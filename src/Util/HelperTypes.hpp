#pragma once

#include <Util/Geometry.hpp>
#include <Shaders/include/shadercompat.h>

// TODO: consider using numbers here instead?
typedef std::string RenderTargetIdentifier;
typedef std::string ShaderPassIdentifier;

template<typename T>   
class stm_ptr {
private:
	T* mPtr = nullptr;
	uint16_t* mCounter = nullptr;

public:
	inline stm_ptr() {};
	inline stm_ptr(T* ptr) : mPtr(ptr) { if (mPtr) mCounter = new uint16_t(1); }
	inline stm_ptr(const stm_ptr& p) : mPtr(p.mPtr), mCounter(p.mCounter) { if (mCounter) (*mCounter)++; }
	inline stm_ptr(stm_ptr&& p) : mPtr(p.mPtr), mCounter(p.mCounter) { p.mPtr = nullptr; p.mCounter = nullptr; }
	inline ~stm_ptr() { reset(); }

	template<typename Tx>
	inline friend stm_ptr<Tx> stm_ptr_cast(const stm_ptr& src) {
		if (dynamic_cast<Tx*>(src.mPtr)) {
			stm_ptr cpy(src);
			return *(stm_ptr<Tx>*)(&cpy);
		}
		return nullptr;
	}
	
	inline T* get() const { return mPtr; }
	
	inline void reset() {
		if (mCounter == nullptr) return;
		(*mCounter)--;
		if (*mCounter == 0) {
			delete mCounter;
			delete mPtr;
		}
		mCounter = nullptr;
		mPtr = nullptr;
	}

	inline T& operator*() const { return *mPtr; }
	inline T* operator->() const { return mPtr; }

	inline operator T*() const { return mPtr; }
	inline operator bool() const { return mPtr; }

	inline bool operator ==(const stm_ptr& rhs) const { return mPtr == rhs.mPtr; }
	inline bool operator ==(const T* rhs) const { return mPtr == rhs; }

	inline stm_ptr& operator =(T* rhs) {
		if (rhs == mPtr) return *this;
		reset();
		mPtr = rhs;
		if (mPtr) mCounter = new uint16_t(1);
		return *this;
	}
	inline stm_ptr& operator =(const stm_ptr& rhs) {
		if (rhs.get() == mPtr) return *this;
		reset();
		mPtr = rhs.mPtr;
		mCounter = rhs.mCounter;
		if (mCounter) (*mCounter)++;
		return *this;
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

// Represents a pipeline with various parameters, within a shader
struct PipelineInstance {
	public:
	const uint64_t mRenderPassHash;
	const uint32_t mSubpassIndex;
	const vk::PrimitiveTopology mTopology;
	const vk::CullModeFlags mCullMode;
	const vk::PolygonMode mPolygonMode;
	const vk::PipelineVertexInputStateCreateInfo mVertexInput;

	PipelineInstance() = delete;
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

namespace std {
	template<typename BitType>
	struct hash<vk::Flags<BitType>> {
		inline std::size_t operator()(const vk::Flags<BitType>& v) const {
			size_t h = 0;
			hash_combine(h, (vk::Flags<BitType>::MaskType)v);
			return h;
		}
	};

	template<typename T>
	struct hash<stm_ptr<T>> {
		inline std::size_t operator()(const stm_ptr<T>& v) const { 
			return (std::size_t)v.get();
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
}