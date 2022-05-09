#include "Pipeline.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

Pipeline::Pipeline(const string& name, const vk::ArrayProxy<const Shader::Specialization>& shaders, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers) : DeviceResource(shaders.begin()->shader().mDevice, name) {
  ProfilerRegion ps("Pipeline::Pipeline");
  mShaders.resize(shaders.size());
  ranges::copy(shaders, mShaders.begin());

  vector<vk::PushConstantRange> pcranges;
  vector<unordered_map<uint32_t, DescriptorSetLayout::Binding>> bindings;

  for (const auto& shader : mShaders) {
    // gather bindings
    for (const auto&[id, binding] : shader.shader().descriptors()) {
      uint32_t c = 1;
      for (const auto& v : binding.mArraySize)
        if (v.index() == 0)
          // array size is a literal
          c *= get<uint32_t>(v);
        else
          // array size is a specialization constant
          c *= get<uint32_t>(shader.specialization_constants().at(get<string>(v)));

      auto flag_it = shader.descriptor_binding_flags().find(id);

      if (bindings.size() <= binding.mSet) bindings.resize(binding.mSet + 1);
      auto it = bindings[binding.mSet].find(binding.mBinding);
      if (it == bindings[binding.mSet].end())
        it = bindings[binding.mSet].emplace(binding.mBinding, DescriptorSetLayout::Binding(binding.mDescriptorType, c, {}, shader.shader().stage(), (flag_it != shader.descriptor_binding_flags().end()) ? flag_it->second : vk::DescriptorBindingFlags{} )).first;
      else {
        it->second.mStageFlags |= shader.shader().stage();
        if (flag_it != shader.descriptor_binding_flags().end())
          it->second.mBindingFlags |= flag_it->second;
        if (it->second.mDescriptorCount != c) throw logic_error("Shader modules contain descriptors with different counts at the same binding");
        if (it->second.mDescriptorType != binding.mDescriptorType) throw logic_error("Shader modules contain descriptors of different types at the same binding");
      }
      if (auto s = immutableSamplers.find(id); s != immutableSamplers.end())
        it->second.mImmutableSamplers.push_back(s->second);
    }

    // gather push constants
    if (!shader.shader().push_constants().empty()) {
      uint32_t rangeBegin = numeric_limits<uint32_t>::max();
      uint32_t rangeEnd = 0;
      for (const auto& [id, p] : shader.shader().push_constants()) {
        uint32_t sz = p.mTypeSize;
        if (!p.mArraySize.empty()) {
          sz = p.mArrayStride;
          for (const auto& v : p.mArraySize)
            if (v.index() == 0)
              // array size is a literal
              sz *= get<uint32_t>(v);
            else
              // array size is a specialization constant
              sz *= get<uint32_t>(shader.specialization_constants().at(get<string>(v)));
        }
        rangeBegin = std::min(rangeBegin, p.mOffset);
        rangeEnd   = std::max(rangeEnd  , p.mOffset + sz);

        if (auto it = mPushConstants.find(id); it != mPushConstants.end()) {
          if (it->second.offset != p.mOffset || it->second.size != sz)
            throw logic_error("Push constant [" + id + ", stage = " + to_string(shader.shader().stage()) + " offset = " + to_string(p.mOffset) + ", size = " + to_string(sz) + " ] does not match push constant found other stage(s): " + to_string(it->second));
          it->second.stageFlags |= shader.shader().stage();
        } else
          mPushConstants.emplace(id, vk::PushConstantRange(shader.shader().stage(), p.mOffset, sz));
      }
      pcranges.emplace_back(shader.shader().stage(), rangeBegin, rangeEnd);
    }
  }

  // create descriptorsetlayouts
  mDescriptorSetLayouts.resize(bindings.size());
  for (uint32_t i = 0; i < bindings.size(); i++)
    mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(mDevice, name+"/DescriptorSet"+to_string(i), bindings[i]);

  vector<vk::DescriptorSetLayout> layouts(mDescriptorSetLayouts.size());
  ranges::transform(mDescriptorSetLayouts, layouts.begin(), &DescriptorSetLayout::operator const vk::DescriptorSetLayout &);

  mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcranges));
}

Pipeline::Pipeline(const string& name, const vk::ArrayProxy<const Shader::Specialization>& shaders, const vk::ArrayProxy<shared_ptr<DescriptorSetLayout>>& descriptorSetLayouts) : DeviceResource(shaders.begin()->shader().mDevice, name) {
  ProfilerRegion ps("Pipeline::Pipeline");
  mShaders.resize(shaders.size());
  ranges::copy(shaders, mShaders.begin());

  mDescriptorSetLayouts.resize(descriptorSetLayouts.size());
  ranges::copy(descriptorSetLayouts, mDescriptorSetLayouts.begin());

  vector<vk::PushConstantRange> pcranges;
  vector<unordered_map<uint32_t, DescriptorSetLayout::Binding>> bindings;

  for (const auto& shader : mShaders) {
    // gather push constants
    if (!shader.shader().push_constants().empty()) {
      uint32_t rangeBegin = numeric_limits<uint32_t>::max();
      uint32_t rangeEnd = 0;
      for (const auto& [id, p] : shader.shader().push_constants()) {
        uint32_t sz = p.mTypeSize;
        if (!p.mArraySize.empty()) {
          sz = p.mArrayStride;
          for (const auto& v : p.mArraySize)
            if (v.index() == 0)
              // array size is a literal
              sz *= get<uint32_t>(v);
            else
              // array size is a specialization constant
              sz *= get<uint32_t>(shader.specialization_constants().at(get<string>(v)));
        }
        rangeBegin = std::min(rangeBegin, p.mOffset);
        rangeEnd   = std::max(rangeEnd  , p.mOffset + sz);

        if (auto it = mPushConstants.find(id); it != mPushConstants.end()) {
          if (it->second.offset != p.mOffset || it->second.size != sz)
            throw logic_error("Push constant [" + id + ", stage = " + to_string(shader.shader().stage()) + " offset = " + to_string(p.mOffset) + ", size = " + to_string(sz) + " ] does not match push constant found other stage(s): " + to_string(it->second));
          it->second.stageFlags |= shader.shader().stage();
        } else
          mPushConstants.emplace(id, vk::PushConstantRange(shader.shader().stage(), p.mOffset, sz));
      }
      pcranges.emplace_back(shader.shader().stage(), rangeBegin, rangeEnd);
    }
  }

  vector<vk::DescriptorSetLayout> layouts(mDescriptorSetLayouts.size());
  ranges::transform(mDescriptorSetLayouts, layouts.begin(), &DescriptorSetLayout::operator const vk::DescriptorSetLayout &);

  mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcranges));
}

ComputePipeline::ComputePipeline(const string& name, const Shader::Specialization& shader, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers) : Pipeline(name, shader, immutableSamplers) {
  ProfilerRegion ps("ComputePipeline::ComputePipeline");

  vector<uint32_t> data;
  vector<vk::SpecializationMapEntry> entries;
  shader.fill_specialization_info(entries, data);

  vk::SpecializationInfo specializationInfo((uint32_t)entries.size(), entries.data(), data.size()*sizeof(uint32_t), data.data());
  vk::PipelineShaderStageCreateInfo stageInfo({}, shader.shader().stage(), shader.shader_module(), shader.shader().compile_specializations() ? "main" : shader.shader().entry_point().c_str(), specializationInfo.mapEntryCount ? &specializationInfo : nullptr);
  mPipeline = mDevice->createComputePipeline(mDevice.pipeline_cache(), vk::ComputePipelineCreateInfo({}, stageInfo, mLayout)).value;
  mDevice.set_debug_name(mPipeline, name);
}
ComputePipeline::ComputePipeline(const string& name, const Shader::Specialization& shader, const vk::ArrayProxy<shared_ptr<DescriptorSetLayout>>& descriptorSetLayouts) : Pipeline(name, shader, descriptorSetLayouts) {
  ProfilerRegion ps("ComputePipeline::ComputePipeline");

  vector<uint32_t> data;
  vector<vk::SpecializationMapEntry> entries;
  shader.fill_specialization_info(entries, data);
  vk::SpecializationInfo specializationInfo((uint32_t)entries.size(), entries.data(), data.size()*sizeof(uint32_t), data.data());

  vk::PipelineShaderStageCreateInfo stageInfo({}, shader.shader().stage(), shader.shader_module(), shader.shader().compile_specializations() ? "main" : shader.shader().entry_point().c_str(), specializationInfo.mapEntryCount ? &specializationInfo : nullptr);
  mPipeline = mDevice->createComputePipeline(mDevice.pipeline_cache(), vk::ComputePipelineCreateInfo({}, stageInfo, mLayout)).value;
  mDevice.set_debug_name(mPipeline, name);
}

GraphicsPipeline::GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex, const VertexLayoutDescription& vertexDescription,
		const vk::ArrayProxy<const Shader::Specialization>& shaders, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers,
		const vk::PipelineRasterizationStateCreateInfo& rasterState, bool sampleShading,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState, const vector<vk::PipelineColorBlendAttachmentState>& blendStates,
		const vector<vk::DynamicState>& dynamicStates) : Pipeline(name, shaders, immutableSamplers) {
  ProfilerRegion ps("GraphicsPipeline::GraphicsPipeline");

  vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
  for (const auto& desc : renderPass.subpasses()[subpassIndex] | views::values) {
    if (get<AttachmentType>(desc) == AttachmentType::eColor || get<AttachmentType>(desc) == AttachmentType::eDepthStencil)
      sampleCount = get<vk::AttachmentDescription>(desc).samples;
    if (blendStates.empty() && get<AttachmentType>(desc) == AttachmentType::eColor)
      mBlendStates.emplace_back(get<vk::PipelineColorBlendAttachmentState>(desc));
  }
  if (!blendStates.empty()) mBlendStates = blendStates;

  // attachments
  for (auto& [id, desc] : renderPass.subpasses()[subpassIndex])
    if (get<AttachmentType>(desc) == AttachmentType::eColor || get<AttachmentType>(desc) == AttachmentType::eDepthStencil) {
      mMultisampleState.rasterizationSamples = get<vk::AttachmentDescription>(desc).samples;
      break;
    }

  // vertex inputs
  mGeometryDescription = vertexDescription;
  vector<vk::VertexInputAttributeDescription> attributes;
  vector<vk::VertexInputBindingDescription> bindings;
  for (const auto& [type, vertexInput] : shaders.front().shader().stage_inputs()) {
    const auto&[attribute, bindingIndex] = mGeometryDescription.mAttributes.at(vertexInput.mAttributeType)[vertexInput.mTypeIndex];
    attributes.emplace_back(vertexInput.mLocation, bindingIndex, attribute.mFormat, attribute.mOffset);
    if (bindings.size() <= bindingIndex) bindings.resize(bindingIndex + 1);
    bindings[bindingIndex].binding = bindingIndex;
    bindings[bindingIndex].inputRate = attribute.mInputRate;
    bindings[bindingIndex].stride = attribute.mStride;
  }
  vk::PipelineVertexInputStateCreateInfo vertexInfo({}, bindings, attributes);

  // specialization constants
  vector<tuple<vk::SpecializationInfo, vector<vk::SpecializationMapEntry>, vector<uint32_t>>> specializationInfos(shaders.size());
  vector<vk::PipelineShaderStageCreateInfo> vkstages(shaders.size());
  for (uint32_t i = 0; i < shaders.size(); i++) {
    auto&[stageInfo,entries,data] = specializationInfos[i];
    const Shader::Specialization& stage = shaders.data()[i];
    stage.fill_specialization_info(entries, data);
    stageInfo = vk::SpecializationInfo((uint32_t)entries.size(), entries.data(), data.size()*sizeof(uint32_t), data.data());
    vkstages[i] = vk::PipelineShaderStageCreateInfo({}, stage.shader().stage(), stage.shader_module(), stage.shader().compile_specializations() ? "main" : stage.shader().entry_point().c_str(), stageInfo.mapEntryCount ? &stageInfo : nullptr);
  }
  mDepthStencilState = depthStencilState;
  mDynamicStates = dynamicStates;
  mInputAssemblyState = { {}, vertexDescription.mTopology };
  mViewportState = { {}, 1, nullptr, 1, nullptr };
  mRasterizationState = rasterState;
  mMultisampleState = { {}, sampleCount, sampleShading };
  vk::PipelineColorBlendStateCreateInfo blendState({}, false, vk::LogicOp::eCopy, mBlendStates);
  vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);
  mPipeline = mDevice->createGraphicsPipeline(mDevice.pipeline_cache(),
    vk::GraphicsPipelineCreateInfo({}, vkstages, &vertexInfo, &mInputAssemblyState, nullptr, &mViewportState,
    &mRasterizationState, &mMultisampleState, &mDepthStencilState, &blendState, &dynamicState, mLayout, *renderPass, subpassIndex)).value;
  mDevice.set_debug_name(mPipeline, name);
}