#include "Buffer.hpp"
#include "CommandBuffer.hpp"

using namespace stm;

Buffer::Buffer(Buffer&& v) : DeviceResource(v.mDevice,v.name()), mBuffer(v.mBuffer), mMemory(move(v.mMemory)), mSize(v.mSize), mUsage(v.mUsage), mSharingMode(v.mSharingMode), mTexelViews(move(v.mTexelViews)) {
  v.mBuffer = nullptr;
  v.mSize = 0;
}
Buffer::Buffer(const shared_ptr<Device::MemoryAllocation>& memory, const string& name, vk::BufferUsageFlags usage, vk::SharingMode sharingMode)
  : DeviceResource(memory->mDevice, name), mSize(memory->size()), mUsage(usage), mSharingMode(sharingMode) {
  mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsage, mSharingMode));
  mDevice.set_debug_name(mBuffer, name);
  mMemory = memory;
  vmaBindBufferMemory(mDevice.allocator(), mMemory->allocation(), mBuffer);
}
Buffer::Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage, uint32_t alignment, vk::SharingMode sharingMode)
  : DeviceResource(device, name), mSize(size), mUsage(usage), mSharingMode(sharingMode)  {
  mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsage, mSharingMode));
  mDevice.set_debug_name(mBuffer, name);
  if (memoryUsage != VMA_MEMORY_USAGE_UNKNOWN) {
    vk::MemoryRequirements requirements = mDevice->getBufferMemoryRequirements(mBuffer);
    if (alignment != 0) requirements.alignment = align_up(requirements.alignment, alignment);
    bind_memory(make_shared<Device::MemoryAllocation>(mDevice, requirements, memoryUsage));
  }
}
Buffer::~Buffer() {
  for (auto it = mTexelViews.begin(); it != mTexelViews.end(); it++)
    mDevice->destroyBufferView(it->second);
  mDevice->destroyBuffer(mBuffer);
}

const vk::BufferView& Buffer::TexelView::operator*() const {
  if (auto it = buffer()->mTexelViews.find(mHashKey); it != buffer()->mTexelViews.end())
    return it->second;
  else {
    vk::BufferView v = buffer()->mDevice->createBufferView(vk::BufferViewCreateInfo({}, **buffer(), mFormat, offset(), size_bytes()));
    buffer()->mDevice.set_debug_name(v, buffer()->name()+"/View");
    return buffer()->mTexelViews.emplace(mHashKey, v).first->second;
  }
}