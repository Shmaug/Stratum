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

struct fRect2D {
	union {
		float v[4];
		float4 xyzw;
		struct {
			// location
			float2 mOffset;
			// full size of rectangle
			float2 mExtent;
		};
	};

	inline fRect2D() : mOffset(0), mExtent(0) {};
	inline fRect2D(const float2& offset, const float2& extent) : mOffset(offset), mExtent(extent) {};
	inline fRect2D(float ox, float oy, float ex, float ey) : mOffset(float2(ox, oy)), mExtent(ex, ey) {};

	inline fRect2D& operator=(const fRect2D & rhs) {
		mOffset = rhs.mOffset;
		mExtent = rhs.mExtent;
		return *this;
	}

	inline bool Intersects(const fRect2D& p) const {
		return !(
			mOffset.x + mExtent.x < p.mOffset.x ||
			mOffset.y + mExtent.y < p.mOffset.y ||
			mOffset.x > p.mOffset.x + p.mExtent.x ||
			mOffset.y > p.mOffset.y + p.mExtent.y);
	}
	inline bool Contains(const float2& p) const {
		return 
			p.x > mOffset.x && p.y > mOffset.y &&
			p.x < mOffset.x + mExtent.x && p.y < mOffset.y + mExtent.y;
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


// Defines a vertex input. Hashes itself once at creation, then remains immutable.
struct VertexInput {
public:
	const std::vector<vk::VertexInputBindingDescription> mBindings;
	// Note: In order to hash and compare correctly, attributes must appear in order of location.
	const std::vector<vk::VertexInputAttributeDescription> mAttributes;

	VertexInput(const std::vector<vk::VertexInputBindingDescription>& bindings, const std::vector<vk::VertexInputAttributeDescription>& attribs)
		: mBindings(bindings), mAttributes(attribs) {
		std::size_t h = 0;
		for (const auto& b : mBindings) {
			hash_combine(h, b.binding);
			hash_combine(h, b.inputRate);
			hash_combine(h, b.stride);
		}
		for (const auto& a : mAttributes) {
			hash_combine(h, a.binding);
			hash_combine(h, a.format);
			hash_combine(h, a.location);
			hash_combine(h, a.offset);
		}
		mHash = h;
	};

	inline bool operator==(const VertexInput& rhs) const {
		/*
		if (mBinding.binding != rhs.mBinding.binding ||
			mBinding.inputRate != rhs.mBinding.inputRate ||
			mBinding.stride != rhs.mBinding.stride ||
			mAttributes.size() != rhs.mAttributes.size()) return false;
		for (uint32_t i = 0; i < mAttributes.size(); i++)
			if (mAttributes[i].binding != rhs.mAttributes[i].binding ||
				mAttributes[i].format != rhs.mAttributes[i].format ||
				mAttributes[i].location != rhs.mAttributes[i].location ||
				mAttributes[i].offset != rhs.mAttributes[i].offset) return false;
		return true;
		*/
		return mHash == rhs.mHash;
	}

private:
	friend struct std::hash<VertexInput>;
	size_t mHash;
};

// Represents a pipeline with various parameters, within a shader
struct PipelineInstance {
	public:
	const uint64_t mRenderPassHash;
	const uint32_t mSubpassIndex;
	const VertexInput* mVertexInput;
	const vk::PrimitiveTopology mTopology;
	const vk::CullModeFlags mCullMode;
	const vk::PolygonMode mPolygonMode;

	inline PipelineInstance(uint64_t renderPassHash, uint32_t subpassIndex, const VertexInput* vertexInput, vk::PrimitiveTopology topology, vk::CullModeFlags cullMode, vk::PolygonMode polyMode)
		: mRenderPassHash(renderPassHash), mSubpassIndex(subpassIndex), mVertexInput(vertexInput), mTopology(topology), mCullMode(cullMode), mPolygonMode(polyMode) {
			// Compute hash once upon creation
			mHash = 0;
			hash_combine(mHash, mRenderPassHash);
			hash_combine(mHash, mSubpassIndex);
			if (mVertexInput) hash_combine(mHash, *mVertexInput);
			hash_combine(mHash, mTopology);
			hash_combine(mHash, mCullMode);
			hash_combine(mHash, mPolygonMode);
		};

	inline bool operator==(const PipelineInstance& rhs) const { return rhs.mHash == mHash; }
	
private:
	friend struct std::hash<PipelineInstance>;
	size_t mHash;
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
	struct hash<fRect2D> {
		inline std::size_t operator()(const fRect2D& v) const {
			size_t h = 0;
			hash_combine(h, v.mExtent);
			hash_combine(h, v.mOffset);
			return h;
		}
	};
	
	template<>
	struct hash<VertexInput> {
		inline std::size_t operator()(const  VertexInput& v) const {
			return v.mHash;
		}
	};
	
	template<>
	struct hash<PipelineInstance> {
		inline std::size_t operator()(const PipelineInstance& p) const { return p.mHash; }
	};

	template<typename T>
	struct hash<variant_ptr<T>> {
		inline std::size_t operator()(const variant_ptr<T>& v) const {
			return hash<T*>(v.get());
		}
	};
}