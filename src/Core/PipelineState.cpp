#include "PipelineState.hpp"

using namespace stm;

uint32_t PipelineState::descriptor_count(const string& name) const {
	for (const auto&[stage, spirv] : mShaders)
		if (auto it = spirv->descriptors().find(name); it != spirv->descriptors().end()) {
			uint32_t count = 1;
			for (const auto& v : it->second.mArraySize)
				if (v.index() == 0)
					// array size is a literal
					count *= get<uint32_t>(v);
				else
					// array size is a specialization constant
					count *= specialization_constant(get<string>(v));
			return count;
		}
	return 0;
}

stm::Descriptor& PipelineState::descriptor(const string& name, uint32_t arrayIndex) {		
	auto desc_it = mDescriptors.find(name);
	if (desc_it != mDescriptors.end()) {
		auto it = desc_it->second.find(arrayIndex);
		if (it != desc_it->second.end())
			return it->second;
	}

	for (const auto&[stage, spirv] : mShaders)
		if (spirv->descriptors().find(name) != spirv->descriptors().end()) {
			auto desc_it = mDescriptors.emplace(name, unordered_map<uint32_t, stm::Descriptor>()).first;
			auto it = desc_it->second.emplace(arrayIndex, stm::Descriptor()).first;
			return it->second;
		}
	
	throw invalid_argument("Descriptor " + name + " does not exist");
}

void PipelineState::transition_images(CommandBuffer& commandBuffer) const {
	for (auto& [name, p] : mDescriptors) {

		vk::PipelineStageFlags firstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
		for (const auto&[stage, spirv] : mShaders) {
			vk::PipelineStageFlags pipelineStage;
			switch (stage) {
				default:
				case vk::ShaderStageFlagBits::eVertex:
					pipelineStage = vk::PipelineStageFlagBits::eVertexShader;
					break;
				case vk::ShaderStageFlagBits::eGeometry:
					pipelineStage = vk::PipelineStageFlagBits::eGeometryShader;
					break;
				case vk::ShaderStageFlagBits::eTessellationControl:
					pipelineStage = vk::PipelineStageFlagBits::eTessellationControlShader;
					break;
				case vk::ShaderStageFlagBits::eTessellationEvaluation:
					pipelineStage = vk::PipelineStageFlagBits::eTessellationEvaluationShader;
					break;
				case vk::ShaderStageFlagBits::eFragment:
					pipelineStage = vk::PipelineStageFlagBits::eFragmentShader;
					break;
				case vk::ShaderStageFlagBits::eCompute:
					pipelineStage = vk::PipelineStageFlagBits::eComputeShader;
					break;
				case vk::ShaderStageFlagBits::eRaygenKHR:
				case vk::ShaderStageFlagBits::eAnyHitKHR:
				case vk::ShaderStageFlagBits::eIntersectionKHR:
				case vk::ShaderStageFlagBits::eClosestHitKHR:
				case vk::ShaderStageFlagBits::eMissKHR:
					pipelineStage = vk::PipelineStageFlagBits::eRayTracingShaderKHR;
					break;
				case vk::ShaderStageFlagBits::eTaskNV:
					pipelineStage = vk::PipelineStageFlagBits::eTaskShaderNV;
					break;
				case vk::ShaderStageFlagBits::eMeshNV:
					pipelineStage = vk::PipelineStageFlagBits::eMeshShaderNV;
					break;
			}
			if (pipelineStage < firstStage && spirv->descriptors().find(name) != spirv->descriptors().end())
				firstStage = pipelineStage;
		}

		for (auto& [arrayIndex, d] : p)
			if (d.index() == 0) {
				Image::View img = get<Image::View>(d);
				if (img) img.transition_barrier(commandBuffer, firstStage, get<vk::ImageLayout>(d), get<vk::AccessFlags>(d));
			}
	}
}

void PipelineState::bind_descriptor_sets(CommandBuffer& commandBuffer, const unordered_map<string, uint32_t>& dynamicOffsets) {
	ProfilerRegion ps("PipelineState::bind_descriptor_sets");
	
	if (!commandBuffer.bound_framebuffer())
		transition_images(commandBuffer);		

	const Pipeline& pipeline = *commandBuffer.bound_pipeline();

	vector<shared_ptr<DescriptorSet>> descriptorSets(pipeline.descriptor_set_layouts().size());
	for (uint32_t i = 0; i < descriptorSets.size(); i++) {
		// fetch the bindings for the current set index
		unordered_map<string, const ShaderModule::DescriptorBinding*> bindings;
			for (auto& [id, descriptors] : mDescriptors)
				for (const auto& stage : pipeline.shaders())
					if (auto it = stage.mShader->descriptors().find(id); it != stage.mShader->descriptors().end() && it->second.mSet == i)
						bindings[id] = &it->second;
		
		// find DescriptorSets matching current layout
		const shared_ptr<DescriptorSetLayout>& layout = pipeline.descriptor_set_layouts()[i];
		auto[first, last] = mDescriptorSets.equal_range(layout.get());
		for (auto[l, descriptorSet] : ranges::subrange(first, last)) {
			// find outdated/nonexistant descriptors
			bool found = true;
			for (const auto& [id, descriptors] : mDescriptors) {
				for (const auto&[arrayIndex, descriptor] : descriptors) {
					auto binding_it = bindings.find(id);
					if (binding_it == bindings.end()) {
						found = false;
						break;
					}
					const Descriptor* d = descriptorSet->find(binding_it->second->mBinding, arrayIndex);
					if (!d || *d != descriptor) {
						if (descriptorSet->in_use()) {
							found = false;
							break;
						} else {
							// update the descriptor
							descriptorSet->insert_or_assign(binding_it->second->mBinding, arrayIndex, descriptor);
						}
					}
				}
				if (!found) break;
			}
			if (found) {
				descriptorSets[i] = descriptorSet;
				break;
			}
		}
		// create new DescriptorSet if necessary
		if (!descriptorSets[i]) {
			descriptorSets[i] = make_shared<DescriptorSet>(layout, mName+"/DescriptorSet"+to_string(i));
			mDescriptorSets.emplace(layout.get(), descriptorSets[i]);
			for (auto& [id, descriptors] : mDescriptors)
				if (auto it = bindings.find(id); it != bindings.end())
					for (const auto&[arrayIndex, descriptor] : descriptors)
						descriptorSets[i]->insert_or_assign(it->second->mBinding, arrayIndex, descriptor);
		}
	}
	unordered_map<uint32_t, vector<pair<uint32_t, uint32_t>>> offsetMap;
	for (const auto&[id,offset] : dynamicOffsets)
		for (const auto& stage : commandBuffer.bound_pipeline()->shaders())
			if (auto it = stage.mShader->descriptors().find(id); it != stage.mShader->descriptors().end())
				offsetMap[it->second.mSet].emplace_back(make_pair(it->second.mBinding, offset));
	vector<uint32_t> offsets;
	for (uint32_t i = 0; i < descriptorSets.size(); i++) {
		offsets.clear();
		if (auto it = offsetMap.find(i); it != offsetMap.end()) {
			offsets.resize(it->second.size());
			ranges::transform(it->second, offsets.begin(), &pair<uint32_t,uint32_t>::second);
		}
		commandBuffer.bind_descriptor_set(i, descriptorSets[i], offsets);
	}
}

void PipelineState::push_constants(CommandBuffer& commandBuffer) const {
	ProfilerRegion ps("PipelineState::push_constants");
	for (const auto&[name, range] : commandBuffer.bound_pipeline()->push_constants())
		if (auto it = mPushConstants.find(name); it != mPushConstants.end()) {
			const auto& value = it->second;
			if (range.size != value.size()) throw invalid_argument("argument size (" + to_string(value.size()) + ") must match push constant size (" + to_string(range.size) +")");
			commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), range.stageFlags, range.offset, (uint32_t)value.size(), value.data());
		}
}

shared_ptr<ComputePipeline> ComputePipelineState::get_pipeline() {
	ProfilerRegion ps("PipelineState::get_pipeline");

	Pipeline::ShaderSpecialization shader = { mShaders.at(vk::ShaderStageFlagBits::eCompute), mSpecializationConstants, mDescriptorBindingFlags };
	size_t key = 0;
	{
		ProfilerRegion ps("hash_args");
		key = hash_args(shader, mImmutableSamplers);
	}
	
	auto pipeline = find_pipeline(key);
	if (!pipeline) {
		pipeline = make_shared<ComputePipeline>(mName, shader, mImmutableSamplers);
		add_pipeline(key, pipeline);
	}
	return static_pointer_cast<ComputePipeline>(pipeline);
}

shared_ptr<GraphicsPipeline> GraphicsPipelineState::get_pipeline(const RenderPass& renderPass, uint32_t subpassIndex, const VertexLayoutDescription& vertexDescription, vk::ShaderStageFlags stageMask) {
	ProfilerRegion ps("PipelineState::get_pipeline");

	vector<Pipeline::ShaderSpecialization> shaders;
	shaders.reserve(mShaders.size());
	for (const auto& [stage, shader] : mShaders)
		if (stage & stageMask)
			shaders.emplace_back(shader, mSpecializationConstants, mDescriptorBindingFlags);
	
	size_t key = 0;
	{
		ProfilerRegion ps("hash_args");
		key = hash_args(renderPass, subpassIndex, vertexDescription, shaders, mImmutableSamplers, mRasterState, mSampleShading, mDepthStencilState, mBlendStates);
	}

	auto pipeline = find_pipeline(key);
	if (!pipeline) {
		pipeline = make_shared<GraphicsPipeline>(mName, renderPass, subpassIndex, vertexDescription, shaders, mImmutableSamplers, mRasterState, mSampleShading, mDepthStencilState, mBlendStates);
		add_pipeline(key, pipeline);
	}
	return static_pointer_cast<GraphicsPipeline>(pipeline);
}
