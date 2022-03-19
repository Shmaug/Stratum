#include "DescriptorSet.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

void DescriptorSet::flush_writes() {
  if (mPendingWrites.empty()) return;

  vector<variant<vk::DescriptorImageInfo, vk::DescriptorBufferInfo, vk::WriteDescriptorSetInlineUniformBlockEXT, vk::WriteDescriptorSetAccelerationStructureKHR>> infos;
  vector<vk::WriteDescriptorSet> writes;
  writes.reserve(mPendingWrites.size());
  infos.reserve(mPendingWrites.size());
  for (uint64_t idx : mPendingWrites) {
    const auto& binding = mLayout->at(idx >> 32);
    auto& entry = mDescriptors.at(idx);
    auto& write = writes.emplace_back(vk::WriteDescriptorSet(mDescriptorSet, idx >> 32, idx & ~uint32_t(0), 1, binding.mDescriptorType));
    switch (write.descriptorType) {
    case vk::DescriptorType::eInputAttachment:
    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eCombinedImageSampler:
    case vk::DescriptorType::eSampler: {
      vk::DescriptorImageInfo& info = get<vk::DescriptorImageInfo>(infos.emplace_back(vk::DescriptorImageInfo{}));
      info.imageLayout = get<vk::ImageLayout>(entry);
      info.imageView = *get<Image::View>(entry);
      if (write.descriptorType == vk::DescriptorType::eCombinedImageSampler || write.descriptorType == vk::DescriptorType::eSampler)
        info.sampler = **get<shared_ptr<Sampler>>(entry);
      write.pImageInfo = &info;
      break;
    }

    case vk::DescriptorType::eUniformBufferDynamic:
    case vk::DescriptorType::eStorageBufferDynamic:
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eStorageBuffer: {
      vk::DescriptorBufferInfo& info = get<vk::DescriptorBufferInfo>(infos.emplace_back(vk::DescriptorBufferInfo{}));
      const auto& view = get<Buffer::StrideView>(entry);
      info.buffer = **view.buffer();
      info.offset = view.offset();
      if (write.descriptorType == vk::DescriptorType::eUniformBufferDynamic || write.descriptorType == vk::DescriptorType::eStorageBufferDynamic)
        info.range = view.stride();
      else
        info.range = view.size_bytes();
      write.pBufferInfo = &info;
      break;
    }

    case vk::DescriptorType::eUniformTexelBuffer:
    case vk::DescriptorType::eStorageTexelBuffer:
      write.pTexelBufferView = get<Buffer::TexelView>(entry).operator->();
      break;

    case vk::DescriptorType::eInlineUniformBlockEXT: {
      vk::WriteDescriptorSetInlineUniformBlockEXT& info = get<vk::WriteDescriptorSetInlineUniformBlockEXT>(infos.emplace_back(vk::WriteDescriptorSetInlineUniformBlockEXT{}));
      info.setData<byte>(get<vector<byte>>(entry));
      write.descriptorCount = info.dataSize;
      write.pNext = &info;
      break;
    }

    case vk::DescriptorType::eAccelerationStructureKHR: {
      vk::WriteDescriptorSetAccelerationStructureKHR& info = get<vk::WriteDescriptorSetAccelerationStructureKHR>(infos.emplace_back(vk::WriteDescriptorSetAccelerationStructureKHR{}));
      info.setAccelerationStructures(get<vk::AccelerationStructureKHR>(entry));
      write.descriptorCount = info.accelerationStructureCount;
      write.pNext = &info;
      break;
    }
    }
  }
  mDevice->updateDescriptorSets(writes, {});
  mPendingWrites.clear();
}

void DescriptorSet::transition_images(CommandBuffer& commandBuffer, const vk::PipelineStageFlags& dstStage) const {
  for (auto& [arrayIndex, d] : mDescriptors)
    if (d.index() == 0) {
      const Image::View img = get<Image::View>(d);
      if (img) img.transition_barrier(commandBuffer, dstStage, get<vk::ImageLayout>(d), get<vk::AccessFlags>(d));
    }
}