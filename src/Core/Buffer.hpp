#pragma once

#include <Core/Device.hpp>

namespace stm {

class Buffer {
private:
	vk::Buffer mBuffer;

public:
	const std::string mName;
	Device* const mDevice;

	STRATUM_API Buffer(const std::string& name, Device* device, const void* data, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API Buffer(const std::string& name, Device* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API ~Buffer();
	inline vk::Buffer operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }

	// Copy data from the host to the device
	// If this buffer is not host visible, then a staging buffer will be created and the data will be copied with Buffer::CopyFrom()
	// If this buffer is not host visible and does not have the vk::BufferUsageFlagBits::eTransferDst flag, then the buffer will be re-created with vk::BufferUsageFlagBits::eTransferDst
	// If this buffer is host visible, then the data is immediately memcpy'd
	STRATUM_API void Copy(const void* data, vk::DeviceSize size);
	template<typename T>
	inline void Copy(const std::vector<T>& data) { Copy(data.data(), data.size() * sizeof(T)); }
	STRATUM_API void Copy(const Buffer& src);
	STRATUM_API void Copy(const Buffer& src, CommandBuffer& commandBuffer);

	inline void* Mapped() const { return mMemoryBlock.Mapped(); }
	inline vk::DeviceSize Size() const { return mSize; }
	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const Device::Memory::Block& MemoryBlock() const { return mMemoryBlock; }

private:
	Device::Memory::Block mMemoryBlock;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::SharingMode mSharingMode;
};

class BufferView {
public:
	std::shared_ptr<Buffer> mBuffer;
	vk::DeviceSize mByteOffset = 0;
	BufferView() = default;
	BufferView(BufferView&&) = default;
	BufferView(const BufferView&) = default;
	BufferView& operator =(const BufferView&) = default;
	BufferView& operator =(BufferView&&) = default;
	inline BufferView(std::shared_ptr<Buffer> buffer, vk::DeviceSize offset = 0) : mBuffer(buffer), mByteOffset(offset) {};
	inline bool operator==(const BufferView& rhs) const { return mBuffer == rhs.mBuffer && mByteOffset == rhs.mByteOffset; }
};

}

namespace std {
template<>
struct hash<stm::BufferView> {
	inline size_t operator()(const stm::BufferView& v) const {
		return stm::hash_combine(v.mBuffer, v.mByteOffset);
	}
};
}