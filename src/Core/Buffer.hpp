#pragma once

#include "Device.hpp"

namespace stm {

class Buffer {
public:
	Buffer() = delete;
	Buffer(const Buffer&) = delete;
	STRATUM_API Buffer(const string& name, Device& device, const byte_blob& data, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API Buffer(const string& name, Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API ~Buffer();
	inline vk::Buffer operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline stm::Device& Device() const { return mDevice; }

	// Copy data from the host to the device
	// If this buffer is not host visible, then a staging buffer will be created and the data will be copied with Buffer::CopyFrom()
	// If this buffer is not host visible and does not have the vk::BufferUsageFlagBits::eTransferDst flag, then the buffer will be re-created with vk::BufferUsageFlagBits::eTransferDst
	// If this buffer is host visible, then the data is immediately memcpy'd
	STRATUM_API void Copy(const byte_blob& data);
	STRATUM_API void Copy(const byte_blob& data, CommandBuffer& commandBuffer);
	// Copy data from the host to the device
	// If this buffer is not host visible, then a staging buffer will be created and the data will be copied with Buffer::CopyFrom()
	// If this buffer is not host visible and does not have the vk::BufferUsageFlagBits::eTransferDst flag, then the buffer will be re-created with vk::BufferUsageFlagBits::eTransferDst
	// If this buffer is host visible, then the data is immediately memcpy'd
	template<typename T> inline void Copy(const vector<T>& data) { Copy(data.data(), data.size() * sizeof(T)); }

	STRATUM_API void Copy(const Buffer& src);
	STRATUM_API void Copy(const Buffer& src, CommandBuffer& commandBuffer);

	inline void* Mapped() const { return mMemoryBlock.Mapped(); }
	inline vk::DeviceSize Size() const { return mSize; }
	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const Device::Memory::Block& MemoryBlock() const { return mMemoryBlock; }

private:
	vk::Buffer mBuffer;
	stm::Device& mDevice;
	string mName;
	Device::Memory::Block mMemoryBlock;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::SharingMode mSharingMode;
};

class ArrayBufferView {
public:
	shared_ptr<Buffer> mBuffer;
	vk::DeviceSize mBufferOffset;
	vk::DeviceSize mElementSize;
	ArrayBufferView() = default;
	ArrayBufferView(const ArrayBufferView&) = default;
	inline ArrayBufferView(shared_ptr<Buffer> buffer, vk::DeviceSize bufferOffset, vk::DeviceSize elementSize) : mBuffer(buffer), mBufferOffset(bufferOffset), mElementSize(elementSize) {};
	ArrayBufferView& operator=(const ArrayBufferView&) = default;
	ArrayBufferView& operator=(ArrayBufferView&& v) = default;
	inline bool operator==(const ArrayBufferView& rhs) const = default;
};

}