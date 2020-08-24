#pragma once

#include <Util/Util.hpp>

class Buffer {
private:
	vk::Buffer mBuffer;

public:
	const std::string mName;

	STRATUM_API Buffer(const std::string& name, ::Device* device, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API Buffer(const std::string& name, ::Device* device, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::Format viewFormat, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API Buffer(const std::string& name, ::Device* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API Buffer(const std::string& name, ::Device* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::Format viewFormat, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API Buffer(const Buffer& src);
	STRATUM_API ~Buffer();

	// Upload data from the host to the device
	// If this buffer is not host visible, then a staging buffer will be created and the data will be copied with Buffer::CopyFrom()
	// If this buffer is not host visible and does not have the vk::BufferUsageFlagBits::eTransferDst flag, then the buffer will be re-created with vk::BufferUsageFlagBits::eTransferDst
	// If this buffer is host visible, then the data is immediately memcpy'd
	STRATUM_API void Upload(const void* data, vk::DeviceSize size);

	inline void* MappedData() const { return mMemory.mMapped; }

	inline vk::DeviceSize Size() const { return mSize; }
	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const DeviceMemoryAllocation& Memory() const { return mMemory; }

	STRATUM_API void CopyFrom(const Buffer& other);
	Buffer& operator=(const Buffer& other) = delete;

	// The view used for a texel buffer. Can be nullptr if the buffer is not a texel buffer.
	inline const vk::BufferView& View() const { return mView; }
	inline ::Device* Device() const { return mDevice; }
	inline operator vk::Buffer() const { return mBuffer; }

private:
	::Device* mDevice = nullptr;
	DeviceMemoryAllocation mMemory;

	vk::BufferView mView;
	vk::Format mViewFormat = vk::Format::eUndefined;

	vk::DeviceSize mSize = 0;

	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;

	STRATUM_API void Allocate();
};