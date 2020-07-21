#include <Content/Shader.hpp>
#include <Content/Texture.hpp>
#include <Stratum/ShaderCompiler.hpp>

#include <string>

using namespace std;

Shader::Shader(const string& name, ::Device* device, const string& filename)
	: mName(name), mDevice(device), mViewportState({}), mRasterizationState({}), mDynamicState({}), mBlendMode(BLEND_MODE_OPAQUE), mDepthStencilState({}), mPassMask(PASS_MAIN) {
	ifstream file(filename, ios::binary);
	if (!file.is_open()) {
		fprintf_color(COLOR_RED, stderr, "Failed to load shader: %s\n", filename.c_str());
		throw;
	}

	uint32_t threadCount = thread::hardware_concurrency();
	if (threadCount > 0) threadCount = threadCount - 1;

	CompiledShader compiled(file);

	#pragma region Compile shader modules
	printf("%s: Compiling %d shader modules", filename.c_str(), (uint32_t)compiled.mModules.size());
	vector<VkShaderModuleCreateInfo> moduleCreateInfo(compiled.mModules.size());
	vector<VkShaderModule> modules(compiled.mModules.size());
	for (uint32_t i = 0; i < compiled.mModules.size(); i++) {
		moduleCreateInfo[i] = {};
		moduleCreateInfo[i].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo[i].codeSize = compiled.mModules[i].mSpirv.size() * sizeof(uint32_t);
		moduleCreateInfo[i].pCode = compiled.mModules[i].mSpirv.data();
	}
	if (threadCount) {
		uint32_t mc = 0;
		vector<thread> threads;
		for (uint32_t j = 0; j < threadCount; j++) {
			threads.push_back(thread([&, j]() {
				for (uint32_t i = j; i < moduleCreateInfo.size(); i += threadCount) {
					vkCreateShaderModule(*mDevice, &moduleCreateInfo[i], nullptr, &modules[i]);
					mc++;
				}
			}));
		}
		while (mc < moduleCreateInfo.size()) {
			printf("\r%s: Compiling shader modules  %d/%d", filename.c_str(), mc + 1, (uint32_t)compiled.mModules.size());
			this_thread::sleep_for(100ms);
		}
		for (uint32_t i = 0; i < threads.size(); i++) if (threads[i].joinable()) threads[i].join();
	} else {
		for (uint32_t i = 0; i < moduleCreateInfo.size(); i++) {
			vkCreateShaderModule(*mDevice, &moduleCreateInfo[i], nullptr, &modules[i]);
			printf("\r%s: Compiling shader modules  %d/%d", filename.c_str(), i + 1, (uint32_t)compiled.mModules.size());
		}
	}
	printf_color(COLOR_GREEN, "\r%s: Compiled %d shader modules                \n", filename.c_str(), (uint32_t)compiled.mModules.size());
	#pragma endregion

	mPassMask = (PassType)0;

	uint32_t computePipelineCount = 0;

	vector<string> kw(compiled.mVariants.size());
	#pragma region Read shader variants
	// A variant is a shader compiled with a unique set of keywords
	printf("%s: Reading %d variants...", filename.c_str(), (uint32_t)compiled.mVariants.size());
	for (uint32_t v = 0; v < compiled.mVariants.size(); v++) {
		set<string> keywords;

		// Read keywords for this variant
		for (uint32_t i = 0; i < compiled.mVariants[v].mKeywords.size(); i++) {
			string keyword = compiled.mVariants[v].mKeywords[i];
			keywords.insert(keyword);
			mKeywords.insert(keyword);
		}

		// Make unique keyword string by appending the keywords in alphabetical order
		kw[v] = "";
		for (const auto& k : keywords)
			kw[v] += k + " ";

		mPassMask = (PassType)(mPassMask | compiled.mVariants[v].mPass);

		ShaderVariant* var;

		if (compiled.mVariants[v].mPass == 0) {
			computePipelineCount++;

			// compute shader
			ComputeShader*& cv = mComputeVariants[compiled.mVariants[v].mEntryPoints[0]][kw[v]];
			if (!cv) cv = new ComputeShader();

			cv->mEntryPoint = compiled.mVariants[v].mEntryPoints[0];

			cv->mStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			cv->mStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			cv->mStage.pName = cv->mEntryPoint.c_str();
			cv->mStage.module = modules[compiled.mVariants[v].mModules[0]];

			cv->mWorkgroupSize = compiled.mVariants[v].mWorkgroupSize;

			var = cv;
		} else {
			// graphics shader
			GraphicsShader*& gv = mGraphicsVariants[compiled.mVariants[v].mPass][kw[v]];
			if (!gv) gv = new GraphicsShader();

			gv->mShader = this;
			gv->mEntryPoints[0] = compiled.mVariants[v].mEntryPoints[0];
			gv->mEntryPoints[1] = compiled.mVariants[v].mEntryPoints[1];

			gv->mStages[0] = {};
			gv->mStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			gv->mStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			gv->mStages[0].pName = gv->mEntryPoints[0].c_str();
			gv->mStages[0].module = modules[compiled.mVariants[v].mModules[0]];

			gv->mStages[1] = {};
			gv->mStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			gv->mStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			gv->mStages[1].pName = gv->mEntryPoints[1].c_str();
			gv->mStages[1].module = modules[compiled.mVariants[v].mModules[1]];

		
			VkPipelineColorBlendAttachmentState bs = {};
			bs.colorWriteMask = mColorMask;
			switch (mBlendMode) {
			case BLEND_MODE_OPAQUE:
				bs.blendEnable = VK_FALSE;
				bs.colorBlendOp = VK_BLEND_OP_ADD;
				bs.alphaBlendOp = VK_BLEND_OP_ADD;
				bs.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				bs.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case BLEND_MODE_ALPHA:
				bs.blendEnable = VK_TRUE;
				bs.colorBlendOp = VK_BLEND_OP_ADD;
				bs.alphaBlendOp = VK_BLEND_OP_ADD;
				bs.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				bs.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				bs.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				bs.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case BLEND_MODE_ADDITIVE:
				bs.blendEnable = VK_TRUE;
				bs.colorBlendOp = VK_BLEND_OP_ADD;
				bs.alphaBlendOp = VK_BLEND_OP_ADD;
				bs.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			case BLEND_MODE_MULTIPLY:
				bs.blendEnable = VK_TRUE;
				bs.colorBlendOp = VK_BLEND_OP_MULTIPLY_EXT;
				bs.alphaBlendOp = VK_BLEND_OP_MULTIPLY_EXT;
				bs.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				bs.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				break;
			}
			gv->mBlendStates = { bs, bs };	// TODO: read BlendStates

			var = gv;
		}

		var->mDescriptorBindings = compiled.mVariants[v].mDescriptorBindings;
		var->mPushConstants = compiled.mVariants[v].mPushConstants;

		// create DescriptorSetLayout bindings
		// bindings[descriptorset][binding] = VkDescriptorSetLayoutBinding
		vector<vector<VkDescriptorSetLayoutBinding>> bindings;
		vector<vector<VkDescriptorBindingFlagsEXT>> bindingFlags;
		for (auto& b : var->mDescriptorBindings) {
			if (bindings.size() <= b.second.first) bindings.resize((size_t)b.second.first + 1);
			if (bindingFlags.size() <= b.second.first) bindingFlags.resize((size_t)b.second.first + 1);

			bindings[b.second.first].push_back(b.second.second);
			bindingFlags[b.second.first].push_back(b.second.second.descriptorCount > 1 ? VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT : 0);

			// read static samplers
			for (const auto& s : compiled.mVariants[v].mStaticSamplers) {
				if (b.first == s.first) {
					Sampler* sampler = new Sampler(mName + " " + b.first, mDevice, s.second);
					b.second.second.pImmutableSamplers = &sampler->VkSampler();
					bindings[b.second.first].back().pImmutableSamplers = &sampler->VkSampler();
					mStaticSamplers.push_back(sampler);
				}
			}
		}

		// create DescriptorSetLayouts
		var->mDescriptorSetLayouts.resize(bindings.size());
		for (uint32_t b = 0; b < bindings.size(); b++) {			
			VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo = {};
			extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
			extendedInfo.bindingCount = (uint32_t)bindingFlags[b].size();
			extendedInfo.pBindingFlags = bindingFlags[b].data();

			VkDescriptorSetLayoutCreateInfo descriptorSetLayout = {};
			descriptorSetLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayout.pNext = &extendedInfo;
			descriptorSetLayout.bindingCount = (uint32_t)bindings[b].size();
			descriptorSetLayout.pBindings = bindings[b].data();
			vkCreateDescriptorSetLayout(*mDevice, &descriptorSetLayout, nullptr, &var->mDescriptorSetLayouts[b]);
			mDevice->SetObjectName(var->mDescriptorSetLayouts[b], mName + " DescriptorSetLayout", VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
		}

		// Create PipelineLayout
		vector<VkPushConstantRange> constants;
		unordered_map<VkShaderStageFlags, uint2> ranges;
		for (const auto& b : var->mPushConstants) {
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

		VkPipelineLayoutCreateInfo layout = {};
		layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout.setLayoutCount = (uint32_t)var->mDescriptorSetLayouts.size();
		layout.pSetLayouts = var->mDescriptorSetLayouts.data();
		layout.pushConstantRangeCount = (uint32_t)constants.size();
		layout.pPushConstantRanges = constants.data();
		vkCreatePipelineLayout(*mDevice, &layout, nullptr, &var->mPipelineLayout);
		mDevice->SetObjectName(var->mPipelineLayout, mName + " PipelineLayout", VK_OBJECT_TYPE_PIPELINE_LAYOUT);

		printf("\r%s: Reading variants  %d/%d", filename.c_str(), v + 1, (uint32_t)compiled.mVariants.size());
	}
	printf_color(COLOR_GREEN, "\r%s: Read %d variants                  \n", filename.c_str(), (uint32_t)compiled.mVariants.size());
	#pragma endregion

	if (computePipelineCount) {
		printf("%s: Creating %d compute pipelines", filename.c_str(), computePipelineCount);
		uint32_t mc = 0;
		if (threadCount) {
			vector<thread> threads;
			for (uint32_t i = 0; i < threadCount; i++) {
				threads.push_back(thread([&, i](){
					for (uint32_t v = i; v < compiled.mVariants.size(); v += threadCount) {
						if (compiled.mVariants[v].mPass != 0) continue;
						ComputeShader*& cv = mComputeVariants[compiled.mVariants[v].mEntryPoints[0]][kw[v]];
						VkComputePipelineCreateInfo pipeline = {};
						pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
						pipeline.stage = cv->mStage;
						pipeline.layout = cv->mPipelineLayout;
						pipeline.basePipelineIndex = -1;
						pipeline.basePipelineHandle = VK_NULL_HANDLE;
						vkCreateComputePipelines(*mDevice, mDevice->PipelineCache(), 1, &pipeline, nullptr, &cv->mPipeline);
						mDevice->SetObjectName(cv->mPipeline, mName, VK_OBJECT_TYPE_PIPELINE);
						mc++;
					}
				}));
			}
			while (mc < computePipelineCount) {
				printf("\r%s: Creating compute pipelines %d/%d", filename.c_str(), mc + 1, computePipelineCount);
				this_thread::sleep_for(100ms);
			}
			for (uint32_t i = 0; i < threads.size(); i++) if (threads[i].joinable()) threads[i].join();
		} else {
			for (uint32_t v = 0; v < compiled.mVariants.size(); v++) {
				if (compiled.mVariants[v].mPass != 0) continue;
				ComputeShader*& cv = mComputeVariants[compiled.mVariants[v].mEntryPoints[0]][kw[v]];
				VkComputePipelineCreateInfo pipeline = {};
				pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
				pipeline.stage = cv->mStage;
				pipeline.layout = cv->mPipelineLayout;
				pipeline.basePipelineIndex = -1;
				pipeline.basePipelineHandle = VK_NULL_HANDLE;
				vkCreateComputePipelines(*mDevice, mDevice->PipelineCache(), 1, &pipeline, nullptr, &cv->mPipeline);
				mDevice->SetObjectName(cv->mPipeline, mName, VK_OBJECT_TYPE_PIPELINE);
				mc++;
				printf("\r%s: Creating compute pipelines %d/%d", filename.c_str(), mc + 1, computePipelineCount);
			}
		}
		printf("\r%s: Created %d compute pipelines      \n", filename.c_str(), computePipelineCount);
	}

	mViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	mViewportState.viewportCount = 1;
	mViewportState.scissorCount = 1;

	mRasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	mRasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	mRasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	mRasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	mRasterizationState.depthClampEnable = VK_FALSE;
	mRasterizationState.lineWidth = 1.f;

	mDynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
	mDynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	mDynamicState.dynamicStateCount = (uint32_t)mDynamicStates.size();
	mDynamicState.pDynamicStates = mDynamicStates.data();

	mRenderQueue = compiled.mRenderQueue;
	mColorMask = compiled.mColorMask;
	mRasterizationState.cullMode = compiled.mCullMode;
	mRasterizationState.polygonMode = compiled.mFillMode;
	mBlendMode = compiled.mBlendMode;
	mDepthStencilState = compiled.mDepthStencilState;
}
Shader::~Shader() {
	for (auto& g : mStaticSamplers)
		safe_delete(g);

	for (auto& g : mGraphicsVariants) {
		for (auto& v : g.second) {
			for (auto& s : v.second->mPipelines)
				vkDestroyPipeline(*mDevice, s.second, nullptr);
			for (auto& s : v.second->mDescriptorSetLayouts)
				vkDestroyDescriptorSetLayout(*mDevice, s, nullptr);
			vkDestroyPipelineLayout(*mDevice, v.second->mPipelineLayout, nullptr);
			for (auto& s : v.second->mStages)
				vkDestroyShaderModule(*mDevice, s.module, nullptr);
			safe_delete(v.second);
		}
	}
	for (auto& s : mComputeVariants) {
		for (auto& v : s.second) {
			for (auto& l : v.second->mDescriptorSetLayouts)
				vkDestroyDescriptorSetLayout(*mDevice, l, nullptr);
			vkDestroyPipeline(*mDevice, v.second->mPipeline, nullptr);
			vkDestroyPipelineLayout(*mDevice, v.second->mPipelineLayout, nullptr);
			vkDestroyShaderModule(*mDevice, v.second->mStage.module, nullptr);
			safe_delete(v.second);
		}
	}
}

VkPipeline GraphicsShader::GetPipeline(RenderPass* renderPass, const VertexInput* vertexInput, VkPrimitiveTopology topology, VkCullModeFlags cullMode, BlendMode blendMode, VkPolygonMode polyMode) {
	BlendMode blend = blendMode == BLEND_MODE_MAX_ENUM ? mShader->mBlendMode : blendMode;
	VkCullModeFlags cull = cullMode == VK_CULL_MODE_FLAG_BITS_MAX_ENUM ? mShader->mRasterizationState.cullMode : cullMode;
	VkPolygonMode poly = polyMode == VK_POLYGON_MODE_MAX_ENUM ? mShader->mRasterizationState.polygonMode : polyMode;
	PipelineInstance instance(*renderPass, vertexInput, topology, cull, blendMode, poly);

	if (mPipelines.count(instance))
		return mPipelines.at(instance);
	else {
		VkPipelineRasterizationStateCreateInfo rasterState = mShader->mRasterizationState;
		if (cullMode != VK_CULL_MODE_FLAG_BITS_MAX_ENUM) rasterState.cullMode = cullMode;
		if (polyMode != VK_POLYGON_MODE_MAX_ENUM) rasterState.polygonMode = polyMode;

		VkPipelineColorBlendStateCreateInfo blendState = {};
		blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendState.attachmentCount = (uint32_t)mBlendStates.size();
		blendState.pAttachments = mBlendStates.data();

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
		multisampleState.sampleShadingEnable = VK_FALSE;
		multisampleState.alphaToCoverageEnable = VK_FALSE;
		multisampleState.alphaToOneEnable = VK_FALSE;
		multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;

		VkGraphicsPipelineCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = 2;
		info.pStages = mStages;
		info.pInputAssemblyState = &inputAssemblyState;
		info.pVertexInputState = &vinput;
		info.pTessellationState = nullptr;
		info.pViewportState = &mShader->mViewportState;
		info.pRasterizationState = &rasterState;
		info.pMultisampleState = &multisampleState;
		info.pDepthStencilState = &mShader->mDepthStencilState;
		info.pColorBlendState = &blendState;
		info.pDynamicState = &mShader->mDynamicState;
		info.layout = mPipelineLayout;
		info.basePipelineIndex = -1;
		info.basePipelineHandle = VK_NULL_HANDLE;
		info.renderPass = *renderPass;

		#pragma region print
		const char* cullstr = "";
		if (cullMode == VK_CULL_MODE_NONE) cullstr = "VK_CULL_MODE_NONE";
		if (cullMode & VK_CULL_MODE_BACK_BIT) cullstr = "VK_CULL_MODE_BACK";
		if (cullMode & VK_CULL_MODE_FRONT_BIT) cullstr = "VK_CULL_MODE_FRONT";
		if (cullMode == VK_CULL_MODE_FRONT_AND_BACK) cullstr = "VK_CULL_MODE_FRONT_AND_BACK";

		const char* blendstr = "";
		switch (blend) {
		case BLEND_MODE_OPAQUE: blendstr = "Opaque"; break;
		case BLEND_MODE_ALPHA:  blendstr = "Alpha"; break;
		case BLEND_MODE_ADDITIVE: blendstr = "Additive"; break;
		case BLEND_MODE_MULTIPLY: blendstr = "Multiply"; break;
		}

		string kw = "";
		for (const auto& p : mShader->mGraphicsVariants)
			for (const auto& k : p.second)
				if (k.second == this) {
					kw = k.first;
					break;
				}
		printf_color(COLOR_CYAN, "%s [%s]: Generating graphics pipeline %s %s %s\n", mShader->mName.c_str(), kw.c_str(), blendstr, cullstr, TopologyToString(topology));
		#pragma endregion

		VkPipeline p;
		vkCreateGraphicsPipelines(*mShader->mDevice, mShader->mDevice->PipelineCache(), 1, &info, nullptr, &p);
		mShader->mDevice->SetObjectName(p, mShader->mName + "[" + kw + "]", VK_OBJECT_TYPE_PIPELINE);
		mPipelines.emplace(instance, p);

		return p;
	}
}

GraphicsShader* Shader::GetGraphics(PassType pass, const set<string>& keywords) const {
	if (!mGraphicsVariants.count(pass)) return nullptr;
	const auto& v = mGraphicsVariants.at(pass);

	string kw = "";
	for (const auto& k : keywords)
		if (mKeywords.count(k))
			kw += k + " ";

	if (!v.count(kw)) return nullptr;
	return v.at(kw);
}
ComputeShader* Shader::GetCompute(const string& kernel, const set<string>& keywords) const {
	if (!mComputeVariants.count(kernel)) return nullptr;
	auto& k = mComputeVariants.at(kernel);

	string kw = "";
	for (const auto& k : keywords)
		if (mKeywords.count(k))
			kw += k + " ";

	if (!k.count(kw)) return nullptr;
	return k.at(kw);
}