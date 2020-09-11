#include <Data/Pipeline.hpp>
#include <Core/CommandBuffer.hpp>
#include <Scene/Scene.hpp>

using namespace std;

Pipeline::Pipeline(const fs::path& shaderFilename, ::Device* device, const string& name) : Asset(shaderFilename, device, name) {
	ifstream fileStream(shaderFilename, ios::binary);
	if (!fileStream.is_open()) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Could not open file: %s\n", shaderFilename.string().c_str());
		throw;
	}

	mShader = new Shader();
	mShader->Read(fileStream);

	printf("%s: Reading variants..\r", shaderFilename.string().c_str());

	for (ShaderVariant& variant : mShader->mVariants) {
		string keywordString = "";
		for (const string& keyword : variant.mKeywords) {
			mKeywords.insert(keyword);
			keywordString += keyword + " ";
		}

		for (uint32_t i = 0; i < variant.mModules.size(); i++) {
			vk::ShaderModuleCreateInfo moduleCreateInfo = {};
			moduleCreateInfo.codeSize = variant.mModules[i].mSpirvBinary.size() * sizeof(uint32_t);
			moduleCreateInfo.pCode = variant.mModules[i].mSpirvBinary.data();
			variant.mModules[i].mShaderModule = ((vk::Device)*mDevice).createShaderModule(moduleCreateInfo);
		}

		PipelineVariant* var;
		if (variant.mShaderPass.empty()) {
			// Each compute kernel is an entirely different variant
			ComputePipeline*& cv = mComputeVariants[variant.mModules[0].mEntryPoint + "/" + keywordString];
			if (!cv) cv = new ComputePipeline();
			var = cv;
		} else {
			// graphics shader
			GraphicsPipeline*& gv = mGraphicsVariants[variant.mShaderPass + "/" + keywordString];
			if (!gv) gv = new GraphicsPipeline();
			var = gv;
			gv->mDevice = mDevice;
			gv->mName = mName;
		}
		var->mShaderVariant = &variant;
		
		// create DescriptorSetLayout bindings

		vector<vector<vk::DescriptorSetLayoutBinding>> bindings;
		vector<vector<vk::DescriptorBindingFlagsEXT>> bindingFlags;
		for (auto&[bindingName, binding] : variant.mDescriptorSetBindings) {
			if (bindings.size() <= binding.mSet) bindings.resize((size_t)binding.mSet + 1);
			if (bindingFlags.size() <= binding.mSet) bindingFlags.resize((size_t)binding.mSet + 1);

			// Create static samplers
			for (const auto&[samplerName, sampler] : variant.mImmutableSamplers)
				if (bindingName == samplerName)
					binding.mImmutableSamplers.push_back(((vk::Device)*mDevice).createSampler(sampler));
			binding.mBinding.pImmutableSamplers = binding.mImmutableSamplers.data();
			
			bindings[binding.mSet].push_back(binding.mBinding);
			bindingFlags[binding.mSet].push_back(binding.mBinding.descriptorCount > 1 ? vk::DescriptorBindingFlagBits::ePartiallyBound : vk::DescriptorBindingFlags());
		}

		// create DescriptorSetLayouts
		var->mDescriptorSetLayouts.resize(bindings.size());
		for (uint32_t s = 0; s < bindings.size(); s++) {
			var->mDescriptorSetLayouts[s] = nullptr;
			if (bindings[s].empty()) continue;

			vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo = {};
			extendedInfo.bindingCount = (uint32_t)bindingFlags[s].size();
			extendedInfo.pBindingFlags = bindingFlags[s].data();

			vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
			descriptorSetLayoutInfo.pNext = &extendedInfo;
			descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings[s].size();
			descriptorSetLayoutInfo.pBindings = bindings[s].data();
			var->mDescriptorSetLayouts[s] = ((vk::Device)*mDevice).createDescriptorSetLayout(descriptorSetLayoutInfo);
			mDevice->SetObjectName(var->mDescriptorSetLayouts[s], mName + " DescriptorSetLayout" + to_string(s));
		}

		if (!variant.mShaderPass.empty()) { // graphics pipeline
			// resize to fit default descriptor sets
			if (var->mDescriptorSetLayouts.size() < mDevice->DefaultDescriptorSetCount())
				var->mDescriptorSetLayouts.resize(mDevice->DefaultDescriptorSetCount());
			
			// TODO: make PER_CAMERA descriptor set not mandatory
			if (var->mDescriptorSetLayouts[PER_CAMERA]) {
				mDevice->Destroy(var->mDescriptorSetLayouts[PER_CAMERA]);
				var->mDescriptorSetLayouts[PER_CAMERA] = nullptr;
			}

			// use default DescriptorSetLayouts for graphics pipelines
			for (uint32_t s = 0; s < var->mDescriptorSetLayouts.size(); s++)
				if (!var->mDescriptorSetLayouts[s])
					var->mDescriptorSetLayouts[s] = mDevice->DefaultDescriptorSetLayout(s);
		}
		// create empty DescriptorSetLayouts instead of null ones
		for (uint32_t s = 0; s < var->mDescriptorSetLayouts.size(); s++) {
			if (var->mDescriptorSetLayouts[s]) continue;
			var->mDescriptorSetLayouts[s] = ((vk::Device)*mDevice).createDescriptorSetLayout({});
			mDevice->SetObjectName(var->mDescriptorSetLayouts[s], mName + "/DescriptorSetLayout" + to_string(s));
		}

		// Create push constant ranges
		vector<vk::PushConstantRange> constants;
		unordered_map<vk::ShaderStageFlags, uint2> ranges;
		for (const auto&[name, range] : variant.mPushConstants) {
			if (ranges.count(range.stageFlags) == 0)
				ranges[range.stageFlags] = uint2(range.offset, range.offset + range.size);
			else {
				ranges[range.stageFlags].x = min(ranges[range.stageFlags].x, range.offset);
				ranges[range.stageFlags].y = max(ranges[range.stageFlags].y, range.offset + range.size);
			}
		}
		for (auto[flags, range] : ranges) {
			constants.push_back({});
			constants.back().stageFlags = flags;
			constants.back().offset = range.x;
			constants.back().size = range.y - range.x;
		}

		vk::PipelineLayoutCreateInfo layout = {};
		layout.setLayoutCount = (uint32_t)var->mDescriptorSetLayouts.size();
		layout.pSetLayouts = var->mDescriptorSetLayouts.data();
		layout.pushConstantRangeCount = (uint32_t)constants.size();
		layout.pPushConstantRanges = constants.data();
		var->mPipelineLayout = ((vk::Device)*mDevice).createPipelineLayout(layout);
		mDevice->SetObjectName(var->mPipelineLayout, mName + "/PipelineLayout");
	}

	for (auto&[kw, cp] : mComputeVariants) {
		vk::ComputePipelineCreateInfo pipeline = {};
		pipeline.stage.stage = vk::ShaderStageFlagBits::eCompute;
		pipeline.stage.pName = cp->mShaderVariant->mModules[0].mEntryPoint.c_str();
		pipeline.stage.module = cp->mShaderVariant->mModules[0].mShaderModule;
		pipeline.layout = cp->mPipelineLayout;
		pipeline.basePipelineIndex = -1;
		pipeline.basePipelineHandle = nullptr;
		cp->mPipeline = ((vk::Device)*mDevice).createComputePipelines(mDevice->PipelineCache(), { pipeline }).value[0];
		mDevice->SetObjectName(cp->mPipeline, mName);
	}
	printf("Loaded %s (%u variants)\n", shaderFilename.string().c_str(), (uint32_t)mShader->mVariants.size());
}
Pipeline::~Pipeline() {
	for (auto& v : mGraphicsVariants) {
		for (auto& s : v.second->mDescriptorSetLayouts) {
			bool isDefault = false;
			for (uint32_t i = 0; i < mDevice->DefaultDescriptorSetCount(); i++)
				if (s == mDevice->DefaultDescriptorSetLayout(i)) {
					isDefault = true;
					break;
				}
			if (!isDefault)
				mDevice->Destroy(s);
		}
		for (auto& b : v.second->mShaderVariant->mDescriptorSetBindings)
			for (vk::Sampler s : b.second.mImmutableSamplers)
				mDevice->Destroy(s);
		mDevice->Destroy(v.second->mPipelineLayout);
		for (auto& s : v.second->mPipelines) mDevice->Destroy(s.second);
		safe_delete(v.second);
	}
	for (auto& v : mComputeVariants) {
		for (auto& b : v.second->mShaderVariant->mDescriptorSetBindings)
			for (vk::Sampler s : b.second.mImmutableSamplers)
				mDevice->Destroy(s);
		mDevice->Destroy(v.second->mPipelineLayout);
		mDevice->Destroy(v.second->mPipeline);
		for (auto& l : v.second->mDescriptorSetLayouts) mDevice->Destroy(l);
		safe_delete(v.second);
	}

	for (uint32_t i = 0; i < mShader->mVariants.size(); i++)
		for (uint32_t j = 0; j < mShader->mVariants[i].mModules.size(); j++)
			mDevice->Destroy(mShader->mVariants[i].mModules[j].mShaderModule);
		
	delete mShader;
}

vk::Pipeline GraphicsPipeline::GetPipeline(stm_ptr<CommandBuffer> commandBuffer, vk::PrimitiveTopology topology, const vk::PipelineVertexInputStateCreateInfo& vertexInput, vk::Optional<const vk::CullModeFlags> cullModeOverride, vk::Optional<const vk::PolygonMode> polyModeOverride) {
	vk::CullModeFlags cull = cullModeOverride == nullptr ? mShaderVariant->mCullMode : *cullModeOverride;
	vk::PolygonMode poly = polyModeOverride == nullptr ? mShaderVariant->mPolygonMode : *polyModeOverride;
	PipelineInstance instance((uint64_t)hash<RenderPass>()(*commandBuffer->CurrentRenderPass()), commandBuffer->CurrentSubpassIndex(), topology, cull, poly, vertexInput);

	if (mPipelines.count(instance))
		return mPipelines.at(instance);
	else {
		const Subpass& subpass = commandBuffer->CurrentRenderPass()->GetSubpass(commandBuffer->CurrentSubpassIndex());

		vector<vk::PipelineShaderStageCreateInfo> stageInfos(mShaderVariant->mModules.size());
		for (uint32_t i = 0; i < mShaderVariant->mModules.size(); i++) {
			stageInfos[i].stage = mShaderVariant->mModules[i].mStage;
			stageInfos[i].pName = mShaderVariant->mModules[i].mEntryPoint.c_str();
			stageInfos[i].module = mShaderVariant->mModules[i].mShaderModule;
		}
		
		vk::PipelineViewportStateCreateInfo viewportState = {};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
	
		vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth };
		vk::PipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
		dynamicState.pDynamicStates = dynamicStates.data();

		vk::PipelineRasterizationStateCreateInfo rasterizationState = {};
		rasterizationState.cullMode = cull;
		rasterizationState.polygonMode = poly;

		vk::PipelineColorBlendStateCreateInfo blendState = {};
		blendState.attachmentCount = (uint32_t)mShaderVariant->mBlendStates.size();
		blendState.pAttachments = mShaderVariant->mBlendStates.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
		inputAssemblyState.topology = topology;

		vk::PipelineMultisampleStateCreateInfo multisampleState = {};
		multisampleState.sampleShadingEnable = mShaderVariant->mSampleShading;
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;
		for (auto& kp : subpass.mAttachments)
			if (kp.second.mType == AttachmentType::eColor || kp.second.mType == AttachmentType::eDepthStencil) {
				multisampleState.rasterizationSamples = kp.second.mSamples;
				break;
			}

		vk::GraphicsPipelineCreateInfo info = {};
		info.stageCount = (uint32_t)stageInfos.size();
		info.pStages = stageInfos.data();
		info.pInputAssemblyState = &inputAssemblyState;
		info.pVertexInputState = &vertexInput;
		info.pViewportState = &viewportState;
		info.pRasterizationState = &rasterizationState;
		info.pMultisampleState = &multisampleState;
		info.pDepthStencilState = &mShaderVariant->mDepthStencilState;
		info.pColorBlendState = &blendState;
		info.pDynamicState = &dynamicState;
		info.layout = mPipelineLayout;
		info.renderPass = *commandBuffer->CurrentRenderPass();
		info.subpass = commandBuffer->CurrentSubpassIndex();

		vk::Pipeline p = ((vk::Device)*mDevice).createGraphicsPipelines(mDevice->PipelineCache(), { info }).value[0];
		mDevice->SetObjectName(p, mName);
		mPipelines.emplace(instance, p);

		return p;
	}
}

GraphicsPipeline* Pipeline::GetGraphics(const string& pass, const set<string>& keywords) const {
	string key = pass + "/";
	for (const string& k : keywords)
		if (mKeywords.count(k))
			key += k + " ";
	if (!mGraphicsVariants.count(key)) return nullptr;
	return mGraphicsVariants.at(key);
}
ComputePipeline* Pipeline::GetCompute(const string& kernel, const set<string>& keywords) const {
	string key = kernel + "/";
	for (const string& k : keywords)
		if (mKeywords.count(k))
			key += k + " ";
	if (!mComputeVariants.count(key)) return nullptr;
	return mComputeVariants.at(key);
}