#include <Core/Pipeline.hpp>
#include <Core/CommandBuffer.hpp>
#include <Scene/Scene.hpp>

using namespace std;

Pipeline::Pipeline(const string& name, ::Device* device, const string& shaderFilename) : mName(name), mDevice(device) {
	ifstream fileStream(shaderFilename, ios::binary);
	if (!fileStream.is_open()) {
		fprintf_color(COLOR_RED, stderr, "Failed to load shader: %s\n", shaderFilename.c_str());
		throw;
	}

	mShader = new Shader();
	mShader->Read(fileStream);

	printf("%s: Reading variants..\r", shaderFilename.c_str());

	for (ShaderVariant& variant : mShader->mVariants) {
		string keywordString = "";
		for (const string& keyword : variant.mKeywords) {
			mKeywords.insert(keyword);
			keywordString += keyword + " ";
		}

		for (uint32_t i = 0; i < variant.mModules.size(); i++) {
			VkShaderModuleCreateInfo moduleCreateInfo = {};
			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.codeSize = variant.mModules[i].mSpirvBinary.size() * sizeof(uint32_t);
			moduleCreateInfo.pCode = variant.mModules[i].mSpirvBinary.data();
			ThrowIfFailed(vkCreateShaderModule(*mDevice, &moduleCreateInfo, nullptr, &variant.mModules[i].mShaderModule), "vkCreateShaderModule failed");
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

		vector<vector<VkDescriptorSetLayoutBinding>> bindings;
		vector<vector<VkDescriptorBindingFlagsEXT>> bindingFlags;
		for (auto& binding : variant.mDescriptorSetBindings) {
			if (bindings.size() <= binding.second.mSet) bindings.resize((size_t)binding.second.mSet + 1);
			if (bindingFlags.size() <= binding.second.mSet) bindingFlags.resize((size_t)binding.second.mSet + 1);

			// Create static samplers
			for (const auto& sampler : variant.mImmutableSamplers)
				if (binding.first == sampler.first) {
					binding.second.mImmutableSamplers.push_back({});
					vkCreateSampler(*mDevice, &sampler.second, nullptr, &binding.second.mImmutableSamplers.back());
				}
			binding.second.mBinding.pImmutableSamplers = binding.second.mImmutableSamplers.data();
			
			bindings[binding.second.mSet].push_back(binding.second.mBinding);
			bindingFlags[binding.second.mSet].push_back(binding.second.mBinding.descriptorCount > 1 ? VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT : 0);
		}

		// create DescriptorSetLayouts
		var->mDescriptorSetLayouts.resize(bindings.size());
		for (uint32_t s = 0; s < bindings.size(); s++) {
			var->mDescriptorSetLayouts[s] = VK_NULL_HANDLE;
			if (bindings[s].empty()) continue;

			VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo = {};
			extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
			extendedInfo.bindingCount = (uint32_t)bindingFlags[s].size();
			extendedInfo.pBindingFlags = bindingFlags[s].data();

			VkDescriptorSetLayoutCreateInfo descriptorSetLayout = {};
			descriptorSetLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayout.pNext = &extendedInfo;
			descriptorSetLayout.bindingCount = (uint32_t)bindings[s].size();
			descriptorSetLayout.pBindings = bindings[s].data();
			vkCreateDescriptorSetLayout(*mDevice, &descriptorSetLayout, nullptr, &var->mDescriptorSetLayouts[s]);
			mDevice->SetObjectName(var->mDescriptorSetLayouts[s], mName + " DescriptorSetLayout" + to_string(s), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
		}

		// Create push constant ranges
		vector<VkPushConstantRange> constants;
		unordered_map<VkShaderStageFlags, uint2> ranges;
		for (const auto& b : variant.mPushConstants) {
			if (ranges.count(b.second.stageFlags) == 0)
				ranges[b.second.stageFlags] = uint2(b.second.offset, b.second.offset + b.second.size);
			else {
				ranges[b.second.stageFlags].x = min(ranges[b.second.stageFlags].x, b.second.offset);
				ranges[b.second.stageFlags].y = max(ranges[b.second.stageFlags].y, b.second.offset + b.second.size);
			}
		}
		for (auto r : ranges) {
			constants.push_back({});
			constants.back().stageFlags = r.first;
			constants.back().offset = r.second.x;
			constants.back().size = r.second.y - r.second.x;
		}

		if (variant.mShaderPass != "") {
			if (var->mDescriptorSetLayouts.size() <= max(PER_OBJECT, PER_CAMERA)) var->mDescriptorSetLayouts.resize(max(PER_OBJECT, PER_CAMERA) + 1);
			if (!var->mDescriptorSetLayouts[PER_OBJECT]) var->mDescriptorSetLayouts[PER_OBJECT] = mDevice->PerObjectSetLayout();
			if (var->mDescriptorSetLayouts[PER_CAMERA]) vkDestroyDescriptorSetLayout(*mDevice, var->mDescriptorSetLayouts[PER_CAMERA], nullptr);
			// TODO: make this descriptor sets not mandatory
			var->mDescriptorSetLayouts[PER_CAMERA] = mDevice->PerCameraSetLayout();

			// Create dummy descriptors in any slots with null descriptors
			for (uint32_t s = 0; s < var->mDescriptorSetLayouts.size(); s++) {
				if (var->mDescriptorSetLayouts[s] != VK_NULL_HANDLE) continue;
				VkDescriptorSetLayoutCreateInfo descriptorSetLayout = {};
				descriptorSetLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				vkCreateDescriptorSetLayout(*mDevice, &descriptorSetLayout, nullptr, &var->mDescriptorSetLayouts[s]);
				mDevice->SetObjectName(var->mDescriptorSetLayouts[s], mName + " DummyDescriptorSetLayout" + to_string(s), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
			}
		}

		VkPipelineLayoutCreateInfo layout = {};
		layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout.setLayoutCount = (uint32_t)var->mDescriptorSetLayouts.size();
		layout.pSetLayouts = var->mDescriptorSetLayouts.data();
		layout.pushConstantRangeCount = (uint32_t)constants.size();
		layout.pPushConstantRanges = constants.data();
		ThrowIfFailed(vkCreatePipelineLayout(*mDevice, &layout, nullptr, &var->mPipelineLayout), "vkCreatePipelineLayout failed");
		mDevice->SetObjectName(var->mPipelineLayout, mName + " PipelineLayout", VK_OBJECT_TYPE_PIPELINE_LAYOUT);
	}

	for (auto& kp : mComputeVariants) {
		VkComputePipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeline.stage.pName = kp.second->mShaderVariant->mModules[0].mEntryPoint.c_str();
		pipeline.stage.module = kp.second->mShaderVariant->mModules[0].mShaderModule;
		pipeline.layout = kp.second->mPipelineLayout;
		pipeline.basePipelineIndex = -1;
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		vkCreateComputePipelines(*mDevice, mDevice->PipelineCache(), 1, &pipeline, nullptr, &kp.second->mPipeline);
		mDevice->SetObjectName(kp.second->mPipeline, mName, VK_OBJECT_TYPE_PIPELINE);
	}
	printf("Loaded %s (%u variants)\n", shaderFilename.c_str(), (uint32_t)mShader->mVariants.size());
}
Pipeline::~Pipeline() {
	for (auto& v : mGraphicsVariants) {
		for (auto& s : v.second->mDescriptorSetLayouts)
			if (s != mDevice->PerObjectSetLayout() && s != mDevice->PerCameraSetLayout())
				vkDestroyDescriptorSetLayout(*mDevice, s, nullptr);
		for (auto& b : v.second->mShaderVariant->mDescriptorSetBindings)
			for (VkSampler s : b.second.mImmutableSamplers)
				vkDestroySampler(*mDevice, s, nullptr);
		vkDestroyPipelineLayout(*mDevice, v.second->mPipelineLayout, nullptr);
		for (auto& s : v.second->mPipelines) vkDestroyPipeline(*mDevice, s.second, nullptr);
		safe_delete(v.second);
	}
	for (auto& v : mComputeVariants) {
		for (auto& b : v.second->mShaderVariant->mDescriptorSetBindings)
			for (VkSampler s : b.second.mImmutableSamplers)
				vkDestroySampler(*mDevice, s, nullptr);
		vkDestroyPipelineLayout(*mDevice, v.second->mPipelineLayout, nullptr);
		vkDestroyPipeline(*mDevice, v.second->mPipeline, nullptr);
		for (auto& l : v.second->mDescriptorSetLayouts) vkDestroyDescriptorSetLayout(*mDevice, l, nullptr);
		safe_delete(v.second);
	}

	for (uint32_t i = 0; i < mShader->mVariants.size(); i++)
		for (uint32_t j = 0; j < mShader->mVariants[i].mModules.size(); j++)
			vkDestroyShaderModule(*mDevice, mShader->mVariants[i].mModules[j].mShaderModule, nullptr);
		
	delete mShader;
}

VkPipeline GraphicsPipeline::GetPipeline(CommandBuffer* commandBuffer, const VertexInput* vertexInput, VkPrimitiveTopology topology, VkCullModeFlags cullMode, VkPolygonMode polyMode) {
	VkCullModeFlags cull = cullMode == VK_CULL_MODE_FLAG_BITS_MAX_ENUM ? mShaderVariant->mCullMode : cullMode;
	VkPolygonMode poly = polyMode == VK_POLYGON_MODE_MAX_ENUM ? mShaderVariant->mPolygonMode : polyMode;
	PipelineInstance instance((uint64_t)hash<RenderPass>()(*commandBuffer->CurrentRenderPass()), commandBuffer->CurrentSubpassIndex(), vertexInput, topology, cull, poly);

	if (mPipelines.count(instance))
		return mPipelines.at(instance);
	else {
		const Subpass& subpass = commandBuffer->CurrentRenderPass()->GetSubpass(commandBuffer->CurrentSubpassIndex());

		vector<VkPipelineShaderStageCreateInfo> stageInfos(mShaderVariant->mModules.size());
		for (uint32_t i = 0; i < mShaderVariant->mModules.size(); i++) {
			stageInfos[i] = {};
			stageInfos[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfos[i].stage = mShaderVariant->mModules[i].mStage;
			stageInfos[i].pName = mShaderVariant->mModules[i].mEntryPoint.c_str();
			stageInfos[i].module = mShaderVariant->mModules[i].mShaderModule;
		}
		
		VkPipelineRasterizationStateCreateInfo rasterizationState = {};
		rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationState.cullMode = cull;
		rasterizationState.polygonMode = poly;
		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;
		
		vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
		dynamicState.pDynamicStates = dynamicStates.data();

		VkPipelineColorBlendStateCreateInfo blendState = {};
		blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendState.attachmentCount = (uint32_t)mShaderVariant->mBlendStates.size();
		blendState.pAttachments = mShaderVariant->mBlendStates.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
		inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyState.topology = topology;
		inputAssemblyState.primitiveRestartEnable = VK_FALSE;

		VkPipelineVertexInputStateCreateInfo vinput = {};
		vinput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		if (vertexInput) {
			vinput.vertexBindingDescriptionCount = (uint32_t)vertexInput->mBindings.size();
			vinput.pVertexBindingDescriptions = vertexInput->mBindings.data();
			vinput.vertexAttributeDescriptionCount = (uint32_t)vertexInput->mAttributes.size();
			vinput.pVertexAttributeDescriptions = vertexInput->mAttributes.data();
		} else {
			vinput.vertexBindingDescriptionCount = 0;
			vinput.pVertexBindingDescriptions = nullptr;
			vinput.vertexAttributeDescriptionCount = 0;
			vinput.pVertexAttributeDescriptions = nullptr;
		}

		VkPipelineMultisampleStateCreateInfo multisampleState = {};
		multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleState.sampleShadingEnable = mShaderVariant->mSampleShading;
		multisampleState.alphaToCoverageEnable = VK_FALSE;
		multisampleState.alphaToOneEnable = VK_FALSE;
		multisampleState.rasterizationSamples = subpass.mDepthAttachment.first.empty() ? subpass.mColorAttachments.begin()->second.samples : subpass.mDepthAttachment.second.samples;

		VkGraphicsPipelineCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = (uint32_t)stageInfos.size();
		info.pStages = stageInfos.data();
		info.pInputAssemblyState = &inputAssemblyState;
		info.pVertexInputState = &vinput;
		info.pTessellationState = nullptr;
		info.pViewportState = &viewportState;
		info.pRasterizationState = &rasterizationState;
		info.pMultisampleState = &multisampleState;
		info.pDepthStencilState = &mShaderVariant->mDepthStencilState;
		info.pColorBlendState = &blendState;
		info.pDynamicState = &dynamicState;
		info.layout = mPipelineLayout;
		info.renderPass = *commandBuffer->CurrentRenderPass();
		info.subpass = commandBuffer->CurrentSubpassIndex();
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.basePipelineIndex = -1;

		VkPipeline p;
		ThrowIfFailed(vkCreateGraphicsPipelines(*mDevice, mDevice->PipelineCache(), 1, &info, nullptr, &p), "vkCreateGraphicsPipelines failed");
		mDevice->SetObjectName(p, mName, VK_OBJECT_TYPE_PIPELINE);
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