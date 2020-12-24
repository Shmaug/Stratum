#include "Buffer.hpp"
#include "CommandBuffer.hpp"


using namespace stm;

Buffer::Buffer(const string& name, stm::Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SharingMode sharingMode)
	: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(properties), mSharingMode(sharingMode) {
	mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
	mDevice.SetObjectName(mBuffer, mName);
	vk::MemoryRequirements memRequirements = mDevice->getBufferMemoryRequirements(mBuffer);
	mMemoryBlock = mDevice.AllocateMemory(memRequirements, mMemoryProperties, mName);
	mDevice->bindBufferMemory(mBuffer, **mMemoryBlock.mMemory, mMemoryBlock.mOffset);
}
Buffer::Buffer(const string& name, stm::Device& device, const byte_blob& data, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::SharingMode sharingMode)
	: Buffer(name, device, data.size(), usage, properties) { Copy(data); }
Buffer::~Buffer() {
	mDevice->destroyBuffer(mBuffer);
	mDevice.FreeMemory(mMemoryBlock);
}

void Buffer::Copy(const byte_blob& data) {
	CommandBuffer* commandBuffer = mDevice.GetCommandBuffer(mName +"/Copy", vk::QueueFlagBits::eTransfer);
	Copy(data, *commandBuffer);
	mDevice.Execute(commandBuffer, true);
}
void Buffer::Copy(const byte_blob& data, CommandBuffer& commandBuffer) {
	if (data.size() > mSize) throw out_of_range("data size larger than dst buffer");
	if (mMemoryProperties & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
		memcpy(Mapped(), data.data(), data.size());
	else {
		Buffer stagingBuffer(mName + "/Staging", mDevice, data.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		memcpy(stagingBuffer.Mapped(), data.data(), data.size());
		Copy(stagingBuffer, commandBuffer);
	}
}

void Buffer::Copy(const Buffer& src) {
	CommandBuffer* commandBuffer = mDevice.GetCommandBuffer(mName +"/Copy", vk::QueueFlagBits::eTransfer);
	Copy(src, *commandBuffer);
	mDevice.Execute(commandBuffer, true);
}
void Buffer::Copy(const Buffer& src, CommandBuffer& commandBuffer) {
	if (mSize < src.mSize) throw out_of_range("src buffer smaller than dst buffer");
	commandBuffer->copyBuffer(*src, mBuffer, { vk::BufferCopy(0, 0, mSize) });
}