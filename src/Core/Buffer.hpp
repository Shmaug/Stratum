#pragma once

#include "Device.hpp"

namespace stm {

class CommandBuffer;

class Buffer : public DeviceResource {
private:
	vk::Buffer mBuffer;
	shared_ptr<Device::MemoryAllocation> mMemory;
	vk::DeviceSize mSize = 0;
	vk::BufferUsageFlags mUsage;
	vk::SharingMode mSharingMode;

	unordered_map<size_t, vk::BufferView> mTexelViews;
	friend class TexelView;

public:
	inline Buffer(shared_ptr<Device::MemoryAllocation> memory, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(memory->mDevice, name), mSize(size), mUsage(usage), mSharingMode(sharingMode) {
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsage, mSharingMode));
		mDevice.SetObjectName(mBuffer, name);
		mMemory = memory;
		vmaBindBufferMemory(mDevice.allocator(), mMemory->allocation(), mBuffer);
	}
	inline Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: DeviceResource(device, name), mSize(size), mUsage(usage), mSharingMode(sharingMode)  {
		mBuffer = mDevice->createBuffer(vk::BufferCreateInfo({}, mSize, mUsage, mSharingMode));
		mDevice.SetObjectName(mBuffer, name);
		mMemory = make_shared<Device::MemoryAllocation>(mDevice, mDevice->getBufferMemoryRequirements(mBuffer), memoryUsage);
		vmaBindBufferMemory(mDevice.allocator(), mMemory->allocation(), mBuffer);
	}
	inline ~Buffer() {
		for (auto it = mTexelViews.begin(); it != mTexelViews.end(); it++)
			mDevice->destroyBufferView(it->second);
		mDevice->destroyBuffer(mBuffer);
	}
	
	inline const vk::Buffer& operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline operator bool() const { return mBuffer; }

	inline vk::BufferUsageFlags usage() const { return mUsage; }
	inline vk::SharingMode sharing_mode() const { return mSharingMode; }
	inline const auto& memory() const { return mMemory; }

	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const { return mMemory->data(); }

	template<typename T>
	class View {
	private:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mOffset;
		vk::DeviceSize mSize;
	public:
		using value_type = T;
		using size_type = vk::DeviceSize;
		using reference = value_type&;
		using pointer = value_type*;
		using iterator = T*;

		View() = default;
		View(const View&) = default;
		inline View(const shared_ptr<Buffer>& buffer, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: mBuffer(buffer), mOffset(byteOffset), mSize(elementCount == VK_WHOLE_SIZE ? (buffer->size() - byteOffset)/sizeof(T) : elementCount) {
			if (!buffer) throw invalid_argument("buffer cannot be null");
			if (mOffset + mSize > buffer->size()) throw out_of_range("view size out of bounds");
		}

		View& operator=(const View&) = default;
		View& operator=(View&&) = default;
		bool operator==(const View& rhs) const = default;

		inline operator bool() const { return !empty(); }
		
		inline Buffer& buffer() const { return *mBuffer; }
		inline shared_ptr<Buffer> buffer_ptr() const { return mBuffer; }
    inline vk::DeviceSize offset() const { return mOffset; }

    inline bool empty() const { return !mBuffer || mSize == 0; }
		inline size_type size() const { return mSize; }
    inline vk::DeviceSize size_bytes() const { return mSize*sizeof(T); }
		inline pointer data() const { return reinterpret_cast<pointer>(mBuffer->data() + offset()); }
		
		inline T& at(size_type index) const { return data()[index]; }
		inline T& operator[](size_type index) const { return at(index); }

		inline reference front() { return at(0); }
		inline reference back() { return at(mSize - 1); }

		inline iterator begin() const { return data(); }
		inline iterator end() const { return data() + mSize; }
	};

	class StrideView : public View<byte> {
	private:
		vk::DeviceSize mStride;
	public:
		StrideView() = default;
		StrideView(const StrideView&) = default;
		StrideView(StrideView&&) = default;
		inline StrideView(const View<byte>& view, vk::DeviceSize stride) : View<byte>(view), mStride(stride) {}
		inline StrideView(const shared_ptr<Buffer>& buffer, vk::DeviceSize stride, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE) 
			: View<byte>(buffer, byteOffset, elementCount*stride), mStride(stride) {}
		template<typename T> inline StrideView(const View<T>& v) : View<byte>(v.buffer_ptr(), v.offset(), v.size_bytes()), mStride(sizeof(T)) {}
    
		StrideView& operator=(const StrideView&) = default;
		StrideView& operator=(StrideView&&) = default;
		bool operator==(const StrideView& rhs) const = default;

    inline size_t stride() const { return mStride; }
	};

	class TexelView : public View<byte> {
	private:
		vk::Format mFormat;
		size_t mHashKey;

	public:
		TexelView() = default;
		TexelView(const TexelView&) = default;
		TexelView(TexelView&&) = default;
		inline TexelView(const shared_ptr<Buffer>& buf, vk::Format fmt, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE) : View<byte>(buf, byteOffset, elementCount == VK_WHOLE_SIZE ? elementCount = VK_WHOLE_SIZE : elementCount*ElementSize(fmt)), mFormat(fmt) {
			mHashKey = hash_combine(offset(), size_bytes(), mFormat);
		}
		template<typename T> inline TexelView(const View<T>& view, vk::Format fmt) : View<byte>(view.buffer_ptr(), view.offset(), view.size_bytes()), mFormat(fmt) {
			mHashKey = hash_combine(offset(), size_bytes(), mFormat);
		}
		
		inline const vk::BufferView& operator*() const {
			if (auto it = buffer().mTexelViews.find(mHashKey); it != buffer().mTexelViews.end())
				return it->second;
			else {
				vk::BufferView v = buffer().mDevice->createBufferView(vk::BufferViewCreateInfo({}, *buffer(), mFormat, offset(), size_bytes()));
				buffer().mDevice.SetObjectName(v, buffer().name()+"/View");
				return buffer().mTexelViews.emplace(mHashKey, v).first->second;
			}
		}
		inline const vk::BufferView* operator->() const { return &operator*(); }

		TexelView& operator=(const TexelView&) = default;
		TexelView& operator=(TexelView&& v) = default;
		inline bool operator==(const TexelView& rhs) const = default;

		inline vk::Format format() const { return mFormat; }
    inline size_t stride() const { return ElementSize(mFormat); }
	};
};

template<class T>
class buffer_vector {
public:
	using value_type = T;
	using size_type = vk::DeviceSize;
	using reference = value_type&;
	using pointer = value_type*;
	using iterator = T*;
private:
	shared_ptr<Buffer> mBuffer;
	size_type mSize;
	vk::BufferUsageFlags mBufferUsage;
	VmaMemoryUsage mMemoryUsage;
	vk::SharingMode mSharingMode;
public:
	Device& mDevice;

  buffer_vector() = delete;
  buffer_vector(buffer_vector<T>&&) = default;
  inline buffer_vector(const buffer_vector<T>& v) : mDevice(v.mDevice), mSize(v.mSize), mBufferUsage(v.mBufferUsage), mMemoryUsage(v.mMemoryUsage), mSharingMode(v.mSharingMode) {
		if (v.mBuffer) {
			mBuffer = make_shared<Buffer>(mDevice, "buffer_vector", v.size_bytes(), v.buffer_usage(), v.memory_usage(), v.sharing_mode());
			memcpy(mBuffer->data(), v.data(), v.size_bytes());
		}
	}
  inline buffer_vector(Device& device, size_type size, vk::BufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode sharingMode = vk::SharingMode::eExclusive) : mDevice(device), mSize(0), mBufferUsage(bufferUsage), mMemoryUsage(memoryUsage), mSharingMode(sharingMode) {
		if (size) resize(size);
	}
	inline ~buffer_vector() {
		for (size_type i = 0; i < mSize; i++)
			at(i).~T();
	}

	inline operator Buffer::View<byte>() const { return Buffer::View<byte>(mBuffer, 0, size_bytes()); }
	inline operator Buffer::View<T>() const { return Buffer::View<T>(mBuffer, 0, size()); }

	inline size_type empty() const { return mSize == 0; }
	inline size_type size() const { return mSize; }
	inline size_type size_bytes() const { return mSize*sizeof(T); }
	inline size_type capacity() const { return !mBuffer ? 0 : mBuffer->size()/sizeof(T); }
	inline vk::BufferUsageFlags buffer_usage() const { return mBufferUsage; }
	inline VmaMemoryUsage memory_usage() const { return mMemoryUsage; }
	inline vk::SharingMode sharing_mode() const { return mSharingMode; }

	inline void reserve(size_type newcap) {
		if ((!mBuffer && newcap > 0) || newcap*sizeof(T) > mBuffer->size()) {
			vk::MemoryRequirements requirements;
			if (mBuffer) {
				requirements = mBuffer->memory()->requirements();
				requirements.size = max(newcap*sizeof(T), mBuffer->size() + mBuffer->size()/2);
			} else {
				vk::Buffer tmp = mDevice->createBuffer(vk::BufferCreateInfo({}, newcap*sizeof(T), mBufferUsage, mSharingMode));
				requirements = mDevice->getBufferMemoryRequirements(tmp);
				mDevice->destroyBuffer(tmp);
			}
			auto b = make_shared<Buffer>(make_shared<Device::MemoryAllocation>(mDevice, requirements, mMemoryUsage), "buffer_vector", requirements.size, mBufferUsage, mSharingMode);
			if (mBuffer) memcpy(b->data(), mBuffer->data(), mBuffer->size());
			mBuffer = b;
		}
	}
	inline void resize(size_type newsize) {
		if (newsize > mSize) {
			reserve(newsize);
			for (size_type i = mSize; i < newsize; i++)
				new (data() + i) T();
			mSize = newsize;
		} else if (newsize < mSize) {
			for (size_type i = newsize; i < mSize; i++)
				at(i).~T();
			mSize = newsize;
		}
	}
	inline void clear() {
		for (size_type i = 0; i < mSize; i++)
			at(i).~T();
		mSize = 0;
	}

	inline pointer data() { return reinterpret_cast<pointer>(mBuffer->data()); }

	inline reference front() { return *data(); }
	inline reference back() { return *(data() + (mSize - 1)); }

	inline iterator begin() { return data(); }
	inline iterator end() { return data() + mSize; }

	inline reference at(size_type index) {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}
	inline reference operator[](size_type index) {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}

	template<typename... Args> requires(constructible_from<T, Args...>)
	inline reference emplace_back(Args&&... args) {
		while (mSize + 1 > capacity()) reserve(max(1, capacity()*mSize*2));
		return *new (data() + (mSize++)) T(forward<Args>(args)...);
	}
	inline void push_back(const T& value) {
		emplace_back(value);
	}
	inline void push_back(T&& value) {
		emplace_back(forward<T>(value));
	}
	
	inline void pop_back() {
		if (empty()) throw out_of_range("cannot pop empty vector");
		back().~T();
		mSize--;
	}
	inline iterator erase(iterator pos) {
		pos->~T();
		while (pos != &back())
			memcpy(pos, ++pos, sizeof(T));
		mSize--;
	}
};

}