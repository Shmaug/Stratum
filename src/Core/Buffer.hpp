#pragma once

#include "Device.hpp"

namespace stm {

class CommandBuffer;

class Buffer : public DeviceResource {
private:
	vk::Buffer mBuffer;
	shared_ptr<Device::Memory::Block> mMemoryBlock;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::SharingMode mSharingMode;

	unordered_map<size_t, vk::BufferView> mViews;

	friend class TexelBufferView;

public:
	inline Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(device, name), mSize(size), mUsageFlags(usage), mMemoryProperties(memoryFlags), mSharingMode(sharingMode) {
		
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
		mDevice.SetObjectName(mBuffer, Name());
		
		mMemoryBlock = mDevice.AllocateMemory(mDevice->getBufferMemoryRequirements(mBuffer), mMemoryProperties);
		mDevice->bindBufferMemory(mBuffer, *mMemoryBlock->mMemory, mMemoryBlock->mOffset);
	}
	inline ~Buffer() {
		for (auto&[k,v] : mViews) mDevice->destroyBufferView(v);
		mDevice->destroyBuffer(mBuffer);
	}

	inline const vk::Buffer& operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline operator bool() const { return mBuffer; }

	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const {
		if (!(mMemoryProperties & vk::MemoryPropertyFlagBits::eHostVisible)) throw runtime_error("Cannot call data() on buffer that is not host visible");
		return mMemoryBlock->data();
	}

	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline const Device::Memory::Block& Memory() const { return *mMemoryBlock; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }

	class RangeView {
	protected:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mBufferOffset; // in bytes
		vk::DeviceSize mElementCount;
		vk::DeviceSize mElementStride;

	public:
		RangeView() = default;
		RangeView(const RangeView&) = default;
		RangeView(RangeView&&) = default;
		inline RangeView(shared_ptr<Buffer> buffer, vk::DeviceSize elementStride, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: mBuffer(buffer), mBufferOffset(byteOffset), mElementCount(elementCount == VK_WHOLE_SIZE ? (buffer->size() - mBufferOffset)/elementStride : elementCount), mElementStride(elementStride) {}
    
		inline RangeView subrange(size_t firstElement = 0, size_t elementCount = ~size_t(0)) const {
			return RangeView(mBuffer, mBufferOffset + firstElement*stride(), min(elementCount, mElementCount - firstElement), mElementStride);
    }

		RangeView& operator=(const RangeView&) = default;
		RangeView& operator=(RangeView&&) = default;
		bool operator==(const RangeView& rhs) const = default;
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
		inline operator span<T*>() const {
			if (sizeof(T) != mElementStride) throw runtime_error("sizeof(T) must equal buffer stride");
			return span(reinterpret_cast<T*>(data()), mElementCount);
		}

	};
};

class TexelBufferView : public Buffer::RangeView {
private:
	vk::BufferView mView;
	vk::Format mFormat;

public:
	inline const vk::BufferView& operator*() const { return mView; }
	inline const vk::BufferView* operator->() const { return &mView; }

	inline TexelBufferView() = default;
	inline TexelBufferView(const TexelBufferView&) = default;
	inline TexelBufferView(const Buffer::RangeView& view, vk::Format fmt)
		: RangeView(view), mFormat(fmt) {
		size_t key = hash_combine(mBufferOffset, mElementCount, mFormat);
		if (mBuffer->mViews.count(key))
			mView = mBuffer->mViews.at(key);
		else {
			mView = mBuffer->mDevice->createBufferView(vk::BufferViewCreateInfo({}, **mBuffer, mFormat, mBufferOffset, size_bytes()));
			mBuffer->mDevice.SetObjectName(mView, mBuffer->Name()+"/View");
			mBuffer->mViews.emplace(key, mView);
		}
	}
	inline TexelBufferView(shared_ptr<Buffer> buf, vk::Format fmt, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
		: TexelBufferView(RangeView(buf, ElementSize(fmt), byteOffset, elementCount), fmt) {}
	
	TexelBufferView& operator=(const TexelBufferView&) = default;
	TexelBufferView& operator=(TexelBufferView&& v) = default;
	inline bool operator==(const TexelBufferView& rhs) const = default;

	inline vk::Format format() const { return mFormat; }
};

}