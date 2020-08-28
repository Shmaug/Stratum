#pragma once

#include <Util/Geometry.hpp>

#include <Shaders/include/shadercompat.h>

// TODO: consider using numbers here instead?
typedef std::string RenderTargetIdentifier;
typedef std::string ShaderPassIdentifier;

template<typename T>
class variant_ptr {
private:
	T* ptr;
	std::shared_ptr<T> sptr;

public:
	inline variant_ptr() : ptr(nullptr) {};
	inline variant_ptr(T* ptr) : ptr(ptr) {};
	inline variant_ptr(const std::shared_ptr<T>& ptr) : ptr(ptr.get()), sptr(ptr) {};

	inline T* get_shared() const { return sptr; }
	inline T* get() const { return ptr; }
	inline void reset() { ptr = nullptr; sptr.reset(); }

	inline T* operator =(T* rhs) {
    ptr = rhs;
    sptr = nullptr;
		return ptr;
	}
  inline T* operator =(const std::shared_ptr<T>& rhs) {
    ptr = rhs.get();
    sptr = rhs;
		return ptr;
	}
	inline T* operator =(const variant_ptr<T>& rhs) {
    ptr = rhs.ptr;
    sptr = rhs.sptr;
		return ptr;
	}

	inline bool operator ==(const T* rhs) const { return ptr == rhs; }
	inline bool operator ==(const std::shared_ptr<T>& rhs) const { return ptr == rhs.get(); }
	inline bool operator ==(const variant_ptr<T>& rhs) const { return ptr == rhs.ptr; }
	
	inline T* operator ->() const { return ptr; }
};

struct BufferView {
	variant_ptr<Buffer> mBuffer = nullptr;
	vk::DeviceSize mByteOffset = 0;

	BufferView() = default;
	inline BufferView(variant_ptr<Buffer> buffer, vk::DeviceSize offset = 0) : mBuffer(buffer), mByteOffset(offset) {};

	inline bool operator==(const BufferView& rhs) const {
		return mBuffer == rhs.mBuffer && mByteOffset == rhs.mByteOffset;
	}
};

struct fRect2D {
	union {
		float v[4];
		float4 xyzw;
		struct {
			float2 mOffset;
			// full size of rectangle
			float2 mSize;
		};
	};

	inline fRect2D() : mOffset(0), mSize(0) {};
	inline fRect2D(const float2& offset, const float2& extent) : mOffset(offset), mSize(extent) {};
	inline fRect2D(float ox, float oy, float ex, float ey) : mOffset(float2(ox, oy)), mSize(ex, ey) {};

	inline fRect2D& operator=(const fRect2D & rhs) {
		mOffset = rhs.mOffset;
		mSize = rhs.mSize;
		return *this;
	}

	inline bool Intersects(const fRect2D& p) const {
		return !(
			mOffset.x + mSize.x < p.mOffset.x ||
			mOffset.y + mSize.y < p.mOffset.y ||
			mOffset.x > p.mOffset.x + p.mSize.x ||
			mOffset.y > p.mOffset.y + p.mSize.y);
	}
	inline bool Contains(const float2& p) const {
		return 
			p.x > mOffset.x && p.y > mOffset.y &&
			p.x < mOffset.x + mSize.x && p.y < mOffset.y + mSize.y;
	}
};

// Represents a usable region of device memory
struct DeviceMemoryAllocation {
	vk::DeviceMemory mDeviceMemory;
	vk::DeviceSize mOffset;
	vk::DeviceSize mSize;
	uint32_t mMemoryType;
	void* mMapped;
	std::string mTag;
};

// Represents a pipeline with various parameters, within a shader
struct PipelineInstance {
	public:
	const uint64_t mRenderPassHash;
	const uint32_t mSubpassIndex;
	const vk::PrimitiveTopology mTopology;
	const vk::CullModeFlags mCullMode;
	const vk::PolygonMode mPolygonMode;
	const vk::PipelineVertexInputStateCreateInfo mVertexInput;

	inline PipelineInstance(uint64_t renderPassHash, uint32_t subpassIndex, vk::PrimitiveTopology topology, vk::CullModeFlags cullMode, vk::PolygonMode polyMode, const vk::PipelineVertexInputStateCreateInfo& vertexInput)
		: mRenderPassHash(renderPassHash), mSubpassIndex(subpassIndex), mTopology(topology), mCullMode(cullMode), mPolygonMode(polyMode), mVertexInput(vertexInput) {
			// Compute hash once upon creation
			mHash = 0;
			hash_combine(mHash, mRenderPassHash);
			hash_combine(mHash, mSubpassIndex);
			hash_combine(mHash, mTopology);
			hash_combine(mHash, mCullMode);
			hash_combine(mHash, mPolygonMode);
			for (uint32_t i = 0; i < mVertexInput.vertexBindingDescriptionCount; i++) {
				hash_combine(mHash, mVertexInput.pVertexBindingDescriptions[i].binding);
				hash_combine(mHash, mVertexInput.pVertexBindingDescriptions[i].inputRate);
				hash_combine(mHash, mVertexInput.pVertexBindingDescriptions[i].stride);
			}
			for (uint32_t i = 0; i < mVertexInput.vertexAttributeDescriptionCount; i++) {
				hash_combine(mHash, mVertexInput.pVertexAttributeDescriptions[i].binding);
				hash_combine(mHash, mVertexInput.pVertexAttributeDescriptions[i].format);
				hash_combine(mHash, mVertexInput.pVertexAttributeDescriptions[i].location);
				hash_combine(mHash, mVertexInput.pVertexAttributeDescriptions[i].offset);
			}
		};

	inline bool operator==(const PipelineInstance& rhs) const { return rhs.mHash == mHash; }
	
private:
	friend struct std::hash<PipelineInstance>;
	size_t mHash;
};

struct VertexAttribute {
	BufferView mBufferView;
	VertexAttributeType mType;
	uint32_t mTypeIndex;
	uint32_t mElementOffset;
	uint32_t mElementStride;
	vk::VertexInputRate mInputRate;
};

namespace std {
	template<typename BitType>
	struct hash<vk::Flags<BitType>> {
		inline std::size_t operator()(const vk::Flags<BitType>& v) const {
			size_t h = 0;
			hash_combine(h, (vk::Flags<BitType>::MaskType)v);
			return h;
		}
	};

	template<>
	struct hash<BufferView> {
		inline std::size_t operator()(const BufferView& v) const {
			size_t h = 0;
			hash_combine(h, *reinterpret_cast<const size_t*>(&v.mBuffer));
			hash_combine(h, v.mByteOffset);
			return h;
		}
	};

	template<>
	struct hash<fRect2D> {
		inline std::size_t operator()(const fRect2D& v) const {
			size_t h = 0;
			hash_combine(h, v.mSize);
			hash_combine(h, v.mOffset);
			return h;
		}
	};
	
	template<>
	struct hash<PipelineInstance> {
		inline std::size_t operator()(const PipelineInstance& p) const { return p.mHash; }
	};

	template<>
	struct hash<VertexAttribute> {
		inline std::size_t operator()(const VertexAttribute& v) const {
			size_t h = 0;
			hash_combine(h, v.mBufferView);
			hash_combine(h, v.mType);
			hash_combine(h, v.mTypeIndex);
			hash_combine(h, v.mElementOffset);
			hash_combine(h, v.mElementStride);
			return h;
		}
	};

	template<typename T>
	struct hash<variant_ptr<T>> {
		inline std::size_t operator()(const variant_ptr<T>& v) const {
			return hash<T*>(v.get());
		}
	};
}