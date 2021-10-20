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
	Buffer() = delete;
	Buffer(const Buffer&) = delete;
	STRATUM_API Buffer(Buffer&& v);
	STRATUM_API Buffer(const shared_ptr<Device::MemoryAllocation>& memory, const string& name, vk::BufferUsageFlags usage, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API Buffer(Device& device, const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY, uint32_t alignment = 0, vk::SharingMode sharingMode = vk::SharingMode::eExclusive);
	STRATUM_API ~Buffer();
	
	inline vk::Buffer& operator*() { return mBuffer; }
	inline vk::Buffer* operator->() { return &mBuffer; }
	inline const vk::Buffer& operator*() const { return mBuffer; }
	inline const vk::Buffer* operator->() const { return &mBuffer; }
	inline operator bool() const { return mBuffer; }

	inline void bind_memory(const shared_ptr<Device::MemoryAllocation>& mem) {
		mMemory = mem;
  	vmaBindBufferMemory(mDevice.allocator(), mMemory->allocation(), mBuffer);
	}
	inline const shared_ptr<Device::MemoryAllocation>& memory() const { return mMemory; }
	inline vk::BufferUsageFlags usage() const { return mUsage; }
	inline vk::SharingMode sharing_mode() const { return mSharingMode; }

	inline vk::DeviceSize size() const { return mSize; }
	inline byte* data() const { return mMemory->data(); }
	#if VK_KHR_buffer_device_address
	inline vk::DeviceSize device_address() const {
		return mDevice->getBufferAddress(vk::BufferDeviceAddressInfo(mBuffer));
	}
	#endif

	template<typename T = byte>
	class View {
	public:
		using value_type = T;
		using size_type = vk::DeviceSize;
		using reference = value_type&;
		using pointer = value_type*;
		using iterator = T*;

		View() = default;
		View(View&&) = default;
		inline View(const View& v, size_t elementOffset = 0, size_t elementCount = VK_WHOLE_SIZE) : mBuffer(v.mBuffer), mOffset(v.mOffset + elementOffset*sizeof(T)) {
			if (mBuffer) {
				mSize = (elementCount == VK_WHOLE_SIZE) ? (v.size() - elementOffset) : elementCount;
				if (mOffset + mSize*sizeof(T) > mBuffer->size()) throw out_of_range("view size out of bounds");
			}
		}
		inline View(const shared_ptr<Buffer>& buffer, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE) : mBuffer(buffer), mOffset(byteOffset) {
			if (mBuffer) {
				mSize = (elementCount == VK_WHOLE_SIZE) ? (mBuffer->size() - mOffset)/sizeof(T) : elementCount;
				if (mOffset + mSize*sizeof(T) > mBuffer->size()) throw out_of_range("view size out of bounds");
			}
		}
		inline operator View<byte>() const {
			return View<byte>(mBuffer, mOffset, size_bytes());
		}

		View& operator=(const View&) = default;
		View& operator=(View&&) = default;
		bool operator==(const View&) const = default;

		inline operator bool() const { return !empty(); }
		
		inline const shared_ptr<Buffer>& buffer() const { return mBuffer; }
    inline vk::DeviceSize offset() const { return mOffset; }

    inline bool empty() const { return !mBuffer || mSize == 0; }
		inline void reset() { mBuffer.reset(); }
		inline size_type size() const { return mSize; }
    inline vk::DeviceSize size_bytes() const { return mSize*sizeof(T); }
		inline pointer data() const { return reinterpret_cast<pointer>(mBuffer->data() + offset()); }
		#if VK_KHR_buffer_device_address
		inline vk::DeviceSize device_address() const {
			return mBuffer->device_address() + mOffset;
		}
		#endif

		inline T& at(size_type index) const { return data()[index]; }
		inline T& operator[](size_type index) const { return at(index); }

		inline reference front() { return at(0); }
		inline reference back() { return at(mSize - 1); }

		inline iterator begin() const { return data(); }
		inline iterator end() const { return data() + mSize; }
		
	private:
		shared_ptr<Buffer> mBuffer;
		vk::DeviceSize mOffset;
		vk::DeviceSize mSize;
	};

	class StrideView : public View<byte> {
	private:
		vk::DeviceSize mStride;
	public:
		StrideView() = default;
		StrideView(StrideView&&) = default;
		StrideView(const StrideView&) = default;
		inline StrideView(const View<byte>& view, vk::DeviceSize stride) : View<byte>(view), mStride(stride) {}
		template<typename T>
		inline StrideView(const View<T>& v) : View<byte>(v.buffer(), v.offset(), v.size_bytes()), mStride(sizeof(T)) {}
		inline StrideView(const shared_ptr<Buffer>& buffer, vk::DeviceSize stride, vk::DeviceSize byteOffset = 0, vk::DeviceSize byteLength = VK_WHOLE_SIZE) 
			: View<byte>(buffer, byteOffset, byteLength), mStride(stride) {}
    
		StrideView& operator=(const StrideView&) = default;
		StrideView& operator=(StrideView&&) = default;
		bool operator==(const StrideView&) const = default;

    inline size_t stride() const { return mStride; }
		
		template<typename T>
		inline operator View<T>() const {
			if (sizeof(T) != mStride) throw logic_error("sizeof(T) must match stride");
			return Buffer::View<T>(buffer(), offset(), size_bytes()/sizeof(T));
		}
	};

	class TexelView : public View<byte> {
	private:
		vk::Format mFormat;
		size_t mHashKey;

	public:
		TexelView() = default;
		TexelView(TexelView&&) = default;
		TexelView(const TexelView&) = default;
		template<typename T>
		inline TexelView(const View<T>& view, vk::Format fmt)
			: View<byte>(view.buffer(), view.offset(), view.size_bytes()), mFormat(fmt) {
			mHashKey = hash_args(offset(), size_bytes(), mFormat);
		}
		inline TexelView(const shared_ptr<Buffer>& buf, vk::Format fmt, vk::DeviceSize byteOffset = 0, vk::DeviceSize elementCount = VK_WHOLE_SIZE)
			: View<byte>(buf, byteOffset, elementCount == VK_WHOLE_SIZE ? elementCount = VK_WHOLE_SIZE : elementCount*texel_size(fmt)), mFormat(fmt) {
			mHashKey = hash_args(offset(), size_bytes(), mFormat);
		}
		
		STRATUM_API const vk::BufferView& operator*() const;
		inline const vk::BufferView* operator->() const { return &operator*(); }

		TexelView& operator=(const TexelView&) = default;
		TexelView& operator=(TexelView&&) = default;
		bool operator==(const TexelView&) const = default;

		inline vk::Format format() const { return mFormat; }
    inline size_t stride() const { return texel_size(mFormat); }
	};
};

template<class T, size_t _Alignment = 0>
class buffer_vector {
public:
	using value_type = T;
	using size_type = vk::DeviceSize;
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = value_type*;
	using const_pointer = const value_type*;
	using iterator = T*;
	using const_iterator = const T*;
	
	Device& mDevice;

  buffer_vector() = delete;
  inline buffer_vector(Device& device, size_type size = 0, vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eTransferSrc, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
		: mDevice(device), mSize(0), mBufferUsage(bufferUsage), mMemoryUsage(memoryUsage), mSharingMode(sharingMode) {
		if (size) resize(size);
	}
  inline buffer_vector(const buffer_vector<T>& v) : mDevice(v.mDevice), mSize(0), mBufferUsage(v.mBufferUsage), mMemoryUsage(v.mMemoryUsage), mSharingMode(v.mSharingMode) {
		if (v.mSize) {
			reserve(v.mSize);
			for (size_type i = 0; i < v.mSize; i++)
				new (data() + i) T(v[i]);
			mSize = v.mSize;
		}
	}
	inline ~buffer_vector() {
		for (size_type i = 0; i < mSize; i++)
			at(i).~T();
	}

	inline const shared_ptr<Buffer>& buffer() const { return mBuffer; }
	inline Buffer::StrideView buffer_view() const { return Buffer::StrideView(mBuffer, sizeof(T)); }

	inline size_type empty() const { return mSize == 0; }
	inline size_type size() const { return mSize; }
	inline size_type size_bytes() const { return mSize*sizeof(T); }
	inline size_type capacity() const { return !mBuffer ? 0 : mBuffer->size()/sizeof(T); }
	inline vk::BufferUsageFlags buffer_usage() const { return mBufferUsage; }
	inline VmaMemoryUsage memory_usage() const { return mMemoryUsage; }
	inline vk::SharingMode sharing_mode() const { return mSharingMode; }

	inline pointer data() { return reinterpret_cast<pointer>(mBuffer->data()); }
	inline const_pointer data() const { return reinterpret_cast<const pointer>(mBuffer->data()); }

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
			if constexpr (_Alignment > 0)
				requirements.alignment = align_up(requirements.alignment, _Alignment);
			auto b = make_shared<Buffer>(make_shared<Device::MemoryAllocation>(mDevice, requirements, mMemoryUsage), "buffer_vector<"+string(typeid(T).name())+">", mBufferUsage, mSharingMode);
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
	inline void reset() {
		clear();
		mBuffer.reset();
	}

	inline reference front() { return *data(); }
	inline reference back() { return *(data() + (mSize - 1)); }

	inline iterator begin() { return data(); }
	inline const_iterator begin() const { return data(); }
	inline iterator end() { return data() + mSize; }
	inline const_iterator end() const { return data() + mSize; }

	inline reference at(size_type index) {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}
	inline const_reference at(size_type index) const {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}
	inline reference operator[](size_type index) {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}
	inline const_reference operator[](size_type index) const {
		if (index >= mSize) throw out_of_range("index out of bounds");
		return data()[index];
	}

	template<typename... Args> requires(constructible_from<T, Args...>)
	inline reference emplace_back(Args&&... args) {
		while (mSize + 1 > capacity()) reserve(max<size_t>(1, capacity()*mSize*2));
		return *new (data() + (mSize++)) T(forward<Args>(args)...);
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

private:
	shared_ptr<Buffer> mBuffer;
	size_type mSize;
	vk::BufferUsageFlags mBufferUsage;
	VmaMemoryUsage mMemoryUsage;
	vk::SharingMode mSharingMode;
};

}