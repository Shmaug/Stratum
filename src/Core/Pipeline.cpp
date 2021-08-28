#include "Pipeline.hpp"

using namespace stm;
using namespace stm::hlsl;

Pipeline::ShaderStage::ShaderStage(const shared_ptr<SpirvModule>& spirv, const unordered_map<string, uint32_t>& specializationConstants) : mSpirv(spirv) {
  for (const auto&[name, p] : mSpirv->specialization_constants()) {
    const auto[id,defaultValue] = p;
    if (auto it = specializationConstants.find(name); it != specializationConstants.end())
      mSpecializationConstants.emplace(name, it->second);
    else
      mSpecializationConstants.emplace(name, defaultValue);
  }
}

Pipeline::Pipeline(Device& device, const string& name, const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers) : DeviceResource(device, name) {
  mStages.resize(stages.size());
  ranges::copy(stages, mStages.begin());

  vector<vk::PushConstantRange> pcranges;
  vector<unordered_map<uint32_t, DescriptorSetLayout::Binding>> bindings;

  for (const auto& stage : mStages) {
    // gather bindings
    for (const auto&[id, binding] : stage.spirv()->descriptors()) {
      uint32_t c = 1;
      for (const auto& v : binding.mArraySize)
        if (v.index() == 0)
          // array size is a literal
          c *= get<uint32_t>(v);
        else
          // array size is a specialization constant
          c *= stage.specialization_constants().at(get<string>(v));

      if (bindings.size() <= binding.mSet) bindings.resize(binding.mSet + 1);
      auto it = bindings[binding.mSet].find(binding.mBinding);
      if (it == bindings[binding.mSet].end())
        it = bindings[binding.mSet].emplace(binding.mBinding, DescriptorSetLayout::Binding(binding.mDescriptorType, stage.spirv()->stage(), c)).first;
      else {
        it->second.mStageFlags |= stage.spirv()->stage();
        if (it->second.mDescriptorCount != c) throw logic_error("SPIR-V modules contain with different counts at the same binding");
        if (it->second.mDescriptorType != binding.mDescriptorType) throw logic_error("SPIR-V modules contain descriptors of different types at the same binding");
      }
      if (auto s = immutableSamplers.find(id); s != immutableSamplers.end())
        it->second.mImmutableSamplers.push_back(s->second);
    }
    
    // gather push constants
    if (!stage.spirv()->push_constants().empty()) {
      uint32_t rangeBegin = numeric_limits<uint32_t>::max();
      uint32_t rangeEnd = 0;
      for (const auto& [id, p] : stage.spirv()->push_constants()) {
        uint32_t sz = p.mTypeSize;
        if (!p.mArraySize.empty()) {
          sz = p.mArrayStride;
          for (const auto& v : p.mArraySize)
            if (v.index() == 0)
              // array size is a literal
              sz *= get<uint32_t>(v);
            else
              // array size is a specialization constant
              sz *= stage.specialization_constants().at(get<string>(v));
        }
        rangeBegin = std::min(rangeBegin, p.mOffset);
        rangeEnd   = std::max(rangeEnd  , p.mOffset + sz);

        if (auto it = mPushConstants.find(id); it != mPushConstants.end()) {
          if (it->second.offset != p.mOffset || it->second.size != sz)
            throw logic_error("Push constant [" + id + ", stage = " + to_string(stage.spirv()->stage()) + " offset = " + to_string(p.mOffset) + ", size = " + to_string(sz) + " ] does not match push constant found other stage(s): " + to_string(it->second));
          it->second.stageFlags |= stage.spirv()->stage();
        } else
          mPushConstants.emplace(id, vk::PushConstantRange(stage.spirv()->stage(), p.mOffset, sz));
      }
      pcranges.emplace_back(stage.spirv()->stage(), rangeBegin, rangeEnd);
    }
  }

  // create descriptorsetlayouts
  mDescriptorSetLayouts.resize(bindings.size());
  for (uint32_t i = 0; i < bindings.size(); i++)
    mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(device, name+"/DescriptorSet"+to_string(i), bindings[i]);
  
  vector<vk::DescriptorSetLayout> layouts(mDescriptorSetLayouts.size());
  ranges::transform(mDescriptorSetLayouts, layouts.begin(), &DescriptorSetLayout::operator const vk::DescriptorSetLayout &);

  mLayout = mDevice->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcranges));
}

ComputePipeline::ComputePipeline(Device& device, const string& name, const ShaderStage& stage, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers) : Pipeline(device, name, stage, immutableSamplers) {
  vector<uint32_t> data;
  vector<vk::SpecializationMapEntry> entries;
  stage.get_specialization_info(entries, data);
  vk::SpecializationInfo specializationInfo((uint32_t)entries.size(), entries.data(), data.size(), data.data());
  vk::PipelineShaderStageCreateInfo stageInfo({}, stage.spirv()->stage(), **stage.spirv(), stage.spirv()->entry_point().c_str(), specializationInfo.mapEntryCount ? nullptr : &specializationInfo);
  mPipeline = mDevice->createComputePipeline(mDevice.pipeline_cache(), vk::ComputePipelineCreateInfo({}, stageInfo, mLayout)).value;
  mDevice.set_debug_name(mPipeline, name);
}

GraphicsPipeline::GraphicsPipeline(const string& name, const stm::RenderPass& renderPass, uint32_t subpassIndex, const GeometryStateDescription& geometryDescription,
		const vk::ArrayProxy<const ShaderStage>& stages, const unordered_map<string, shared_ptr<Sampler>>& immutableSamplers,
		const vk::PipelineRasterizationStateCreateInfo& rasterState, bool sampleShading,
		const vk::PipelineDepthStencilStateCreateInfo& depthStencilState, const vector<vk::PipelineColorBlendAttachmentState>& blendStates,
		const vector<vk::DynamicState>& dynamicStates) : Pipeline(renderPass.mDevice, name, stages, immutableSamplers) {

  vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
  for (const auto& desc : renderPass.subpasses()[subpassIndex].attachments() | views::values) {
    if (desc.mType == AttachmentType::eColor || desc.mType == AttachmentType::eDepthStencil)
      sampleCount = desc.mDescription.samples;
    if (blendStates.empty() && desc.mType == AttachmentType::eColor)
      mBlendStates.emplace_back(desc.mBlendState);
  }
  if (!blendStates.empty()) mBlendStates = blendStates;

  // attachments
  for (auto& [id, desc] : renderPass.subpasses()[subpassIndex].attachments())
    if (desc.mType == AttachmentType::eColor || desc.mType == AttachmentType::eDepthStencil) {
      mMultisampleState.rasterizationSamples = desc.mDescription.samples;
      break;
    }

  // vertex inputs
  mGeometryDescription = geometryDescription;
  vector<vk::VertexInputAttributeDescription> attributes;
  vector<vk::VertexInputBindingDescription> bindings;
  for (const auto& [type, vertexInput] : stages.front().spirv()->stage_inputs()) {
    const auto&[attribute, bindingIndex] = mGeometryDescription.mAttributes.at(vertexInput.mAttributeType)[vertexInput.mTypeIndex];
    attributes.emplace_back(vertexInput.mLocation, bindingIndex, attribute.mFormat, attribute.mOffset);
    if (bindings.size() <= bindingIndex) bindings.resize(bindingIndex + 1);
    bindings[bindingIndex].binding = bindingIndex;
    bindings[bindingIndex].inputRate = attribute.mInputRate;
    bindings[bindingIndex].stride = attribute.mElementStride;
  }
  vk::PipelineVertexInputStateCreateInfo vertexInfo({}, bindings, attributes);
  
  // specialization constants
  vector<tuple<vk::SpecializationInfo, vector<vk::SpecializationMapEntry>, vector<uint32_t>>> specializationInfos(stages.size());
  vector<vk::PipelineShaderStageCreateInfo> vkstages(stages.size());
  for (uint32_t i = 0; i < stages.size(); i++) {
    const ShaderStage& stage = stages.data()[i];
    auto&[info,entries,data] = specializationInfos[i];
    stage.get_specialization_info(entries, data);
    info = vk::SpecializationInfo((uint32_t)entries.size(), entries.data(), data.size()*sizeof(uint32_t), data.data());
    vkstages[i] = vk::PipelineShaderStageCreateInfo({}, stage.spirv()->stage(), **stage.spirv(), stage.spirv()->entry_point().c_str(), info.mapEntryCount ? &info : nullptr);
  }
  mDepthStencilState = depthStencilState;
  mDynamicStates = dynamicStates;
  mInputAssemblyState = { {}, geometryDescription.mTopology };
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