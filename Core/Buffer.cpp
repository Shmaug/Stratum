#include <Core/Buffer.hpp>
#include <Core/CommandBuffer.hpp>

#include <cstring>

using namespace std;

Buffer::Buffer(const std::string& name, ::Device* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
	: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(properties) {
	Allocate();
}
Buffer::Buffer(const std::string& name, ::Device* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::Format viewFormat, vk::MemoryPropertyFlags properties)
	: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(properties){
	Allocate();
}
Buffer::Buffer(const std::string& name, ::Device* device, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
	: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(properties) {
	if (!(properties & vk::MemoryPropertyFlagBits::eHostVisible))
		mUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;
	Allocate();
	Upload(data, size);
}
Buffer::Buffer(const std::string& name, ::Device* device, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::Format viewFormat, vk::MemoryPropertyFlags properties)
	: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(properties) {
	if (!(properties & vk::MemoryPropertyFlagBits::eHostVisible))
		mUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;
	Allocate();
	Upload(data, size);
}
Buffer::Buffer(const Buffer& src)
	: mName(src.mName), mDevice(src.mDevice), mUsageFlags(src.mUsageFlags | vk::BufferUsageFlagBits::eTransferDst), mMemoryProperties(src.mMemoryProperties), mViewFormat(src.mViewFormat) {
	CopyFrom(src);
}
Buffer::~Buffer() {
	mDevice->Destroy(mView);
	mDevice->Destroy(mBuffer);
	mDevice->FreeMemory(mMemory);
}

void Buffer::Upload(const void* data, vk::DeviceSize size) {
	if (!data || size == 0) return;
	if (size > mSize) throw runtime_error("Data size out of bounds");
	if (mMemoryProperties & vk::MemoryPropertyFlagBits::eHostVisible) {
		memcpy(MappedData(), data, size);
	} else {
		if (!(mUsageFlags & vk::BufferUsageFlagBits::eTransferDst)) {
			mUsageFlags |= vk::BufferUsageFlagBits::eTransferDst;
			
			if (mView) mDevice->Destroy(mView);
			if (mBuffer) mDevice->Destroy(mBuffer);
			mDevice->FreeMemory(mMemory);
			mSize = size;
			Allocate();
		}
		Buffer uploadBuffer(mName + " upload", mDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		memcpy(uploadBuffer.MappedData(), data, size);
		CopyFrom(uploadBuffer);
	}
}

void Buffer::CopyFrom(const Buffer& other) {
	if (mSize != other.mSize) {
		if (mView) mDevice->Destroy(mView);
		if (mBuffer) mDevice->Destroy(mBuffer);
		mDevice->FreeMemory(mMemory);
		mSize = other.mSize;
		Allocate();
	}

	CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();

	vk::BufferMemoryBarrier barrier = {};
	barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
	barrier.buffer = other;
	barrier.size = mSize;
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, barrier);

	vk::BufferCopy copyRegion = {};
	copyRegion.size = mSize;
	commandBuffer->operator vk::CommandBuffer().copyBuffer(other.mBuffer, mBuffer, 1, &copyRegion);
	mDevice->Execute(commandBuffer);
	commandBuffer->Wait();
}

void Buffer::Allocate() {
	vk::Device device = *mDevice;

	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = mSize;
	bufferInfo.usage = mUsageFlags;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	mBuffer = device.createBuffer(bufferInfo);
	mDevice->SetObjectName(mBuffer, mName);

	vk::MemoryRequirements memRequirements = device.getBufferMemoryRequirements(mBuffer);
	mMemory = mDevice->AllocateMemory(memRequirements, mMemoryProperties, mName);
	device.bindBufferMemory(mBuffer, mMemory.mDeviceMemory, mMemory.mOffset);

	if (mViewFormat != vk::Format::eUndefined) {
		vk::BufferViewCreateInfo viewInfo = {};
		viewInfo.buffer = mBuffer;
		viewInfo.offset = 0;
		viewInfo.range = mSize;
		viewInfo.format = mViewFormat;
		mView = device.createBufferView(viewInfo);
		mDevice->SetObjectName(mView, mName + "View");
	}
}