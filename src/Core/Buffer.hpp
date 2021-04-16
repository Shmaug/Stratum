#pragma once

#include "Device.hpp"

namespace stm {

class CommandBuffer;

class Buffer : public DeviceResource {
private:
	vk::Buffer mBuffer;
	shared_ptr<Device::Memory::View> mMemory;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsageFlags;
	vk::SharingMode mSharingMode;

	unordered_map<size_t, vk::BufferView> mTexelViews;

	friend class TexelView;

public:
	inline Buffer(shared_ptr<Device::Memory::View> memory, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(memory->mMemory.mDevice, name), mSize(size), mUsageFlags(usage), mMemory(memory), mSharingMode(sharingMode) {
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
		mDevice.SetObjectName(mBuffer, Name());
		mDevice->bindBufferMemory(mBuffer, *mMemory->mMemory, mMemory->mOffset);
	}
	inline Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(device, name), mSize(size), mUsageFlags(usage), mSharingMode(sharingMode) {
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsageFlags, mSharingMode));
		mDevice.SetObjectName(mBuffer, Name());
		mMemory = mDevice.AllocateMemory(mDevice->getBufferMemoryRequirements(mBuffer), memoryFlags);
		mDevice->bindBufferMemory(mBuffer, *mMemory->mMemory, mMemory->mOffset);
	}
	
	inline ~Buffer() {
		for (auto&[k,v] : mTexelViews) mDevice->destroyBufferView(v);
		mDevice->destroyBuffer(mBuffer);
	}

	inline const vk::Buffer& operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline operator bool() const { return mBuffer; }

	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const { return mMemory->data(); }

	inline vk::BufferUsageFlags Usage() const { return mUsageFlags; }
	inline const Device::Memory::View& Memory() const { return *mMemory; }

	class View {
	protected:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mOffset;
		vk::DeviceSize mSize;
	public:
		View() = default;
		View(const View&) = default;
		View(View&&) = default;
		inline View(shared_ptr<Buffer> buffer, vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE)
			: mBuffer(buffer), mOffset(offset), mSize(size == VK_WHOLE_SIZE ? buffer->size() - offset : size){}

		View& operator=(const View&) = default;
		View& operator=(View&&) = default;
		bool operator==(const View& rhs) const = default;

		inline operator bool() const { return !empty(); }

		inline shared_ptr<Buffer> get() const { return mBuffer; }
		inline Buffer& buffer() const { return *mBuffer; }
    inline vk::DeviceSize offset() const { return mOffset; }
    inline vk::DeviceSize size() const { return mSize; }
    inline bool empty() const { return !mBuffer || mSize == 0; }
		inline byte* data() const { return mBuffer->data() + mOffset; }
	};
	class TexelView : public View {
	private:
		vk::BufferView mView;
		vk::Format mFormat;
	public:
		inline TexelView() = default;
		inline TexelView(const TexelView&) = default;
		inline TexelView(TexelView&&) = default;
		inline TexelView(const View& view, vk::Format fmt) : View(view), mFormat(fmt) {
			size_t key = hash_combine(offset(), size(), mFormat);
			if (mBuffer->mTexelViews.count(key))
				mView = mBuffer->mTexelViews.at(key);
			else {
				mView = mBuffer->mDevice->createBufferView(vk::BufferViewCreateInfo({}, **mBuffer, mFormat, offset(), size()));
				mBuffer->mDevice.SetObjectName(mView, mBuffer->Name()+"/View");
				mBuffer->mTexelViews.emplace(key, mView);
			}
		}
		inline TexelView(shared_ptr<Buffer> buf, vk::Format fmt, vk::DeviceSize offset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: TexelView(View(buf, offset, elementCount*ElementSize(fmt)), fmt) {}
		
		inline const vk::BufferView& operator*() const { return mView; }
		inline const vk::BufferView* operator->() const { return &mView; }

		TexelView& operator=(const TexelView&) = default;
		TexelView& operator=(TexelView&& v) = default;
		inline bool operator==(const TexelView& rhs) const = default;

		inline vk::Format format() const { return mFormat; }
	};
	template<typename T>
	class SpanView : public View {
	public:
		using value_t = T;
		using iterator_t = T*;

		SpanView() = default;
		SpanView(const SpanView&) = default;
		SpanView(SpanView&&) = default;
		inline SpanView(const View& view) : View(view) {}
		inline SpanView(shared_ptr<Buffer> buffer, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: View(View(buffer, byteOffset, elementCount == VK_WHOLE_SIZE ? elementCount : elementCount*sizeof(T))) {}

		SpanView& operator=(const SpanView&) = default;
		SpanView& operator=(SpanView&&) = default;
		bool operator==(const SpanView& rhs) const = default;

    inline size_t stride() const { return sizeof(T); }
		inline T& operator[](size_t index) const { return data()[index]; }

		inline iterator_t begin() const { return (T*)data(); }
		inline iterator_t end() const { return (T*)(data() + size()); }
		inline vk::DeviceSize size() const { return View::size() / sizeof(T); }
		inline size_t size_bytes() const { return View::size(); }
	};
	class StrideView : public View {
	protected:
		vk::DeviceSize mStride;
	public:
		StrideView() = default;
		StrideView(const StrideView&) = default;
		StrideView(StrideView&&) = default;
		inline StrideView(const View& view, vk::DeviceSize stride) : View(view), mStride(stride) {}
		inline StrideView(shared_ptr<Buffer> buffer, vk::DeviceSize stride, vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE)
			: View(View(buffer, offset, size)), mStride(stride) {}
		template<typename T>
		inline StrideView(const SpanView<T>& v) : View(v), mStride(sizeof(T)) {}
    
		StrideView& operator=(const StrideView&) = default;
		StrideView& operator=(StrideView&&) = default;
		bool operator==(const StrideView& rhs) const = default;

    inline size_t stride() const { return mStride; }
	};
};

template<typename T> 
inline Buffer::SpanView<T> make_buffer(Device& device, const string& name, size_t count, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::SharingMode sharingMode = vk::SharingMode::eExclusive) {
	return Buffer::SpanView<T>(make_shared<Buffer>(device, name, count*sizeof(T), usage, memoryFlags, sharingMode), 0, count);
}

template<device_allocator_range R>
inline Buffer::SpanView<ranges::range_value_t<R>> make_buffer(R& range, const string& name, vk::BufferUsageFlags usage, vk::SharingMode sharingMode = vk::SharingMode::eExclusive) {
	return Buffer::SpanView<ranges::range_value_t<R>>(
		make_shared<Buffer>(range.get_allocator().device_memory(ranges::data(range)), name, ranges::size(range)*sizeof(ranges::range_value_t<R>), usage, sharingMode),
		0, ranges::size(range));
}

}