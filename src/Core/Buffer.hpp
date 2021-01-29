#pragma once

#include "Device.hpp"

namespace stm {

class CommandBuffer;

class Buffer {
public:
	Device& mDevice;

	inline Buffer(const string& name, Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: mName(name), mDevice(device), mSize(size), mUsageFlags(usage), mMemoryProperties(memoryFlags), mSharingMode(sharingMode) {
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
		mDevice.SetObjectName(mBuffer, mName);
		vk::MemoryRequirements memRequirements = mDevice->getBufferMemoryRequirements(mBuffer);
		mMemoryBlock = mDevice.AllocateMemory(memRequirements, mMemoryProperties, mName);
		mDevice->bindBufferMemory(mBuffer, **mMemoryBlock.mMemory, mMemoryBlock.mOffset);
	}
	inline ~Buffer() {
		for (auto&[k,v] : mViews) mDevice->destroyBufferView(v);
		mDevice->destroyBuffer(mBuffer);
		mDevice.FreeMemory(mMemoryBlock);
	}

	inline vk::Buffer operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }

	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const { return mMemoryBlock.Mapped(); }
	inline string Name() const { return mName; }

	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline vk::MemoryPropertyFlags MemoryProperties() const { return mMemoryProperties; }
	inline const Device::Memory::Block& MemoryBlock() const { return mMemoryBlock; }

	template<typename T = nullptr_t>
	struct ArrayView {
    using element_type     = T;
    using value_type       = remove_cv_t<element_type>;
    using difference_type  = ptrdiff_t;
    using pointer          = element_type*;
    using const_pointer    = const element_type*;
    using reference        = element_type&;
    using const_reference  = const element_type&;
		using iterator         = element_type*;
    using reverse_iterator = reverse_iterator<iterator>;
		
		ArrayView() = default;
		ArrayView(const ArrayView&) = default;
		ArrayView(ArrayView&&) = default;
		inline ArrayView(shared_ptr<Buffer> buffer, vk::DeviceSize offsetBytes = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE, vk::DeviceSize elementStride = sizeof(value_type))
			: mBuffer(buffer), mBufferOffset(offsetBytes), mElementStride(elementStride), mElementCount(elementCount == VK_WHOLE_SIZE ? (buffer->size() - mBufferOffset)/elementStride : elementCount) {};

		ArrayView& operator=(const ArrayView&) = default;
		ArrayView& operator=(ArrayView&& v) = default;
		bool operator==(const ArrayView& rhs) const = default;
		
		inline Buffer& operator*() const { return *mBuffer; }
		inline Buffer* operator->() const { return mBuffer.get(); }
		inline shared_ptr<Buffer> get() const { return mBuffer; }
		
    inline size_t offset() const { return mBufferOffset; }
    inline size_t stride() const { return is_same_v<value_type,nullptr_t> ? mElementStride : sizeof(value_type); }
    inline size_t size() const { return mElementCount; }
    inline size_t size_bytes() const { return mElementCount*stride(); }
    inline bool empty() const { return !mBuffer || mElementCount == 0; }
		template<typename Ty = T> inline pointer data() const noexcept { return reinterpret_cast<Ty*>(mBuffer->data() + mBufferOffset); }

    inline iterator begin() const { return iterator(data()); }
    inline iterator end() const { return iterator(data() + mElementCount); }
    inline reverse_iterator rbegin() const { return reverse_iterator(end()); }
    inline reverse_iterator rend() const { return reverse_iterator(begin()); }

    inline reference operator[](const size_t i) const { return data()[i]; }
		template<typename Ty = T> inline reference at(const size_t i) const { return data<Ty>()[i]; }
    constexpr reference front() const { return data()[0]; }
    constexpr reference back() const { return data() + (mElementCount - 1); }

    inline ArrayView subview(size_t firstElement = 0, size_t elementCount = ~size_t(0)) const {
        return ArrayView(mBuffer, mBufferOffset + firstElement*stride(), min(elementCount, mElementCount - firstElement));
    }
    inline ArrayView first(const size_t count = 1) const { return ArrayView(mBuffer, mBufferOffset, count); }
    inline ArrayView last(const size_t count = 1) const { return ArrayView(mBuffer, (mElementCount - count)*stride(), count); }
		
		template<typename Ty>
		inline explicit(is_same_v<T,nullptr_t> && sizeof(Ty) != sizeof(T)) operator ArrayView<Ty>() const {
			return ArrayView<Ty>(mBuffer, mBufferOffset, size_bytes()/sizeof(Ty));
		}

		inline operator bool() const { return mBuffer && mElementStride; }

	private:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mBufferOffset;
		vk::DeviceSize mElementCount;
		vk::DeviceSize mElementStride;
	};

private:
	friend class BufferView;

	vk::Buffer mBuffer;
	Device::Memory::Block mMemoryBlock;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::MemoryPropertyFlags mMemoryProperties;
	vk::SharingMode mSharingMode;
	string mName;

	unordered_map<size_t, vk::BufferView> mViews;
};

class BufferView {
private:
	vk::BufferView mView;
	Buffer::ArrayView<> mBuffer;
	vk::Format mFormat;

public:
	inline vk::BufferView operator*() const { return mView; }
	inline const vk::BufferView* operator->() const { return &mView; }

	BufferView() = default;
	BufferView(const BufferView&) = default;
	inline BufferView(Buffer::ArrayView<>& buffer, vk::Format format, vk::BufferViewCreateFlags flags = {}) : mBuffer(buffer), mFormat(format) {
		size_t key = hash_combine(mBuffer.offset(), mBuffer.size(), format, flags);
		if (mBuffer->mViews.count(key) == 0)
			mView = mBuffer->mViews.at(key);
		else {
			mView = mBuffer->mDevice->createBufferView(vk::BufferViewCreateInfo(flags, **mBuffer, format, mBuffer.offset(), mBuffer.size_bytes()));
			mBuffer->mDevice.SetObjectName(mView, mBuffer->Name()+"/BufferView");
			mBuffer->mViews.emplace(key, mView);
		}
	}
	BufferView& operator=(const BufferView&) = default;
	BufferView& operator=(BufferView&& v) = default;
	inline bool operator==(const BufferView& rhs) const = default;
	
	inline explicit operator Buffer::ArrayView<>&() { return mBuffer; }
	inline explicit operator const Buffer::ArrayView<>&() const { return mBuffer; }
};

}