#pragma once

#include "Device.hpp"

namespace stm {

class CommandBuffer;

class Buffer : public DeviceResource {
private:
	vk::Buffer mBuffer;
	Device::Memory::Block mMemoryBlock;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::SharingMode mSharingMode;

	unordered_map<size_t, vk::BufferView> mViews;

	friend class BufferView;

public:
	inline Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(device, name), mSize(size), mUsageFlags(usage), mMemoryProperties(memoryFlags), mSharingMode(sharingMode) {
		
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
		mDevice.SetObjectName(mBuffer, mName);
		
		mMemoryBlock = mDevice.AllocateMemory(mDevice->getBufferMemoryRequirements(mBuffer), mMemoryProperties, mName);
		mDevice->bindBufferMemory(mBuffer, **mMemoryBlock.mMemory, mMemoryBlock.mOffset);
	}
	inline ~Buffer() {
		for (auto&[k,v] : mViews) mDevice->destroyBufferView(v);
		mDevice->destroyBuffer(mBuffer);
		mDevice.FreeMemory(mMemoryBlock);
	}

	inline vk::Buffer operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline operator bool() const { return mBuffer; }

	inline string Name() const { return mName; }
	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const {
		if (!(mMemoryProperties & vk::MemoryPropertyFlagBits::eHostVisible)) throw runtime_error("Cannot call data() on buffer that is not host visible");
		return mMemoryBlock.data();
	}

	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const Device::Memory::Block& MemoryBlock() const { return mMemoryBlock; }

	class ArrayView {
	protected:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mBufferOffset; // in bytes
		vk::DeviceSize mElementCount;
		vk::DeviceSize mElementStride;

	public:
		ArrayView() = default;
		ArrayView(const ArrayView&) = default;
		ArrayView(ArrayView&&) = default;
		inline ArrayView(shared_ptr<Buffer> buffer, vk::DeviceSize elementStride, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: mBuffer(buffer), mBufferOffset(byteOffset), mElementCount(elementCount == VK_WHOLE_SIZE ? (buffer->size() - mBufferOffset)/elementStride : elementCount), mElementStride(elementStride) {}

		ArrayView& operator=(const ArrayView&) = default;
		ArrayView& operator=(ArrayView&&) = default;
		bool operator==(const ArrayView& rhs) const = default;
		inline operator bool() const { return mBuffer && mElementStride; }

		inline shared_ptr<Buffer> get() const { return mBuffer; }
		inline Buffer& buffer() const { return *mBuffer; }
    inline size_t offset() const { return mBufferOffset; }
    inline size_t stride() const { return mElementStride; }
    inline size_t size() const { return mElementCount; }
    inline size_t size_bytes() const { return mElementCount*mElementStride; }
    inline bool empty() const { return !mBuffer || mElementCount == 0; }
		inline byte* data() const { return mBuffer->data() + mBufferOffset; }

		template<typename T>
		inline operator ranges::subrange<T*>() const {
			if (sizeof(T) != mElementStride) throw runtime_error("sizeof(T) must equal buffer stride");
			return ranges::subrange(reinterpret_cast<T*>(data()), mElementCount);
		}
    inline ArrayView subview(size_t firstElement = 0, size_t elementCount = ~size_t(0)) const {
			return ArrayView(mBuffer, mBufferOffset + firstElement*stride(), min(elementCount, mElementCount - firstElement), mElementStride);
    }
	};
};

class BufferView : public Buffer::ArrayView {
private:
	vk::BufferView mView;
	vk::Format mFormat;

public:
	inline vk::BufferView operator*() const { return mView; }
	inline const vk::BufferView* operator->() const { return &mView; }

	inline BufferView() = default;
	inline BufferView(const BufferView&) = default;
	inline BufferView(shared_ptr<Buffer> buf, vk::Format fmt, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
		: ArrayView(buf, ElementSize(fmt), byteOffset, elementCount), mFormat(fmt) {
		size_t key = hash_combine(mBufferOffset, elementCount, mFormat);
		if (mBuffer->mViews.count(key))
			mView = mBuffer->mViews.at(key);
		else {
			mView = mBuffer->mDevice->createBufferView(vk::BufferViewCreateInfo({}, **mBuffer, mFormat, mBufferOffset, size_bytes()));
			mBuffer->mDevice.SetObjectName(mView, mBuffer->Name()+"/View");
			mBuffer->mViews.emplace(key, mView);
		}
	}
	
	BufferView& operator=(const BufferView&) = default;
	BufferView& operator=(BufferView&& v) = default;
	inline bool operator==(const BufferView& rhs) const = default;

	inline vk::Format format() const { return mFormat; }
};

}