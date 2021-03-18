#pragma once

#include "Buffer.hpp"
#include "Pipeline.hpp"
#include "Texture.hpp"

namespace stm {

class DescriptorSet : public DeviceResource {
public:
	class Entry {
		friend class DescriptorSet;
		vk::DescriptorType mType = {};
		union {
			struct {
				TextureView mTextureView;
				vk::ImageLayout mImageLayout;
				shared_ptr<Sampler> mSampler;
			};
			Buffer::ArrayView mBufferView;
			BufferView mTexelBufferView;
			byte_blob mInlineUniformData;
			vk::AccelerationStructureKHR mAccelerationStructure;
		};

		inline void reset() {
			switch (mType) {
			case vk::DescriptorType::eSampler:
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				mTextureView = {};
				mImageLayout = vk::ImageLayout::eUndefined;
				mSampler.reset();
				break;
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				mTexelBufferView = {};
				break;
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				mBufferView = {};
				break;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				mInlineUniformData.clear();
				break;
			case vk::DescriptorType::eAccelerationStructureKHR:
				mAccelerationStructure = nullptr;
				break;
			}
			mType = {};
		}

	public:
		inline Entry(vk::DescriptorType type, const Buffer::ArrayView& buf) : mType(type), mBufferView(buf) {
			if (type != vk::DescriptorType::eUniformBuffer && type != vk::DescriptorType::eStorageBuffer && type != vk::DescriptorType::eUniformBufferDynamic && type != vk::DescriptorType::eStorageBufferDynamic)
				throw invalid_argument("invalid DescriptorType");
		}
		inline Entry(vk::DescriptorType type, const BufferView& buf) : mType(type), mTexelBufferView(buf) {
			if (type != vk::DescriptorType::eStorageTexelBuffer && type != vk::DescriptorType::eUniformTexelBuffer)
				throw invalid_argument("invalid DescriptorType");
		}
		inline Entry(vk::DescriptorType type, const TextureView& tex, vk::ImageLayout layout) : mType(type), mTextureView(tex), mImageLayout(layout), mSampler(nullptr) {
			if (type != vk::DescriptorType::eStorageImage && type != vk::DescriptorType::eSampledImage && type != vk::DescriptorType::eInputAttachment)
				throw invalid_argument("invalid DescriptorType");
		}
		inline Entry(const TextureView& tex, vk::ImageLayout layout, shared_ptr<Sampler> sampler) : mType(vk::DescriptorType::eCombinedImageSampler), mTextureView(tex), mImageLayout(layout), mSampler(sampler) {}
		inline Entry(const byte_blob& inlineData) : mType(vk::DescriptorType::eInlineUniformBlockEXT), mInlineUniformData(inlineData) {}
		inline Entry(vk::AccelerationStructureKHR accelerationStructure) : mType(vk::DescriptorType::eAccelerationStructureKHR), mAccelerationStructure(accelerationStructure) {}
		
		inline Entry() {
			memset(this, 0, sizeof(Entry));
		}
		inline Entry(const Entry& e) { operator=(e); }
		inline Entry(Entry&& e) { operator=(move(e)); }
		inline ~Entry() { reset(); }

		inline Entry& operator=(const Entry& rhs) {
			if (rhs.mType != mType) {
				reset();
				mType = rhs.mType;
			}
			switch (mType) {
			case vk::DescriptorType::eSampler:
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				mTextureView = rhs.mTextureView;
				mImageLayout = rhs.mImageLayout;
				mSampler = rhs.mSampler;
				break;
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				mTexelBufferView = rhs.mTexelBufferView;
				break;
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				mBufferView = rhs.mBufferView;
				break;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				mInlineUniformData = rhs.mInlineUniformData;
				break;
			case vk::DescriptorType::eAccelerationStructureKHR:
				mAccelerationStructure = rhs.mAccelerationStructure;
				break;
			}
			return *this;
		}
		inline Entry& operator=(Entry&& rhs) {
			if (rhs.mType != mType) {
				reset();
				mType = rhs.mType;
			}
			switch (mType) {
			case vk::DescriptorType::eSampler:
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				mTextureView = move(rhs.mTextureView);
				mImageLayout = rhs.mImageLayout;
				mSampler = move(rhs.mSampler);
				break;
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				mTexelBufferView = move(rhs.mTexelBufferView);
				break;
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				mBufferView = move(rhs.mBufferView);
				break;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				mInlineUniformData = move(rhs.mInlineUniformData);
				break;
			case vk::DescriptorType::eAccelerationStructureKHR:
				mAccelerationStructure = rhs.mAccelerationStructure;
				break;
			}
		}

		inline bool operator==(const Entry& rhs) const {
			if (mType != rhs.mType) return false;
			switch (mType) {
			case vk::DescriptorType::eCombinedImageSampler:
				return mTextureView == rhs.mTextureView && mImageLayout == rhs.mImageLayout && mSampler == rhs.mSampler;
			case vk::DescriptorType::eSampler:
				return mSampler == rhs.mSampler;
			case vk::DescriptorType::eInputAttachment:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				return mTextureView == rhs.mTextureView && mImageLayout == rhs.mImageLayout;
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				return mBufferView == rhs.mBufferView;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				return mInlineUniformData == rhs.mInlineUniformData;
			case vk::DescriptorType::eAccelerationStructureKHR:
				return mAccelerationStructure == rhs.mAccelerationStructure;
			}
			return false;
		}
		
		inline operator bool() const {
			switch (mType) {
			case vk::DescriptorType::eCombinedImageSampler:
				return mTextureView && mSampler.get();
			case vk::DescriptorType::eSampler:
				return mSampler.get();
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				return mTextureView;
			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				return mTexelBufferView;
			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				return mBufferView;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				return mInlineUniformData;
			case vk::DescriptorType::eAccelerationStructureKHR:
				return mAccelerationStructure;
			}
			return false;
		}
		inline vk::DescriptorType type() const { return mType; }

		template<typename T> inline const T& get() const { return *(T*)nullptr; }
		template<> inline const vk::DescriptorType& get<vk::DescriptorType>() const {
			return mType;
		}
		template<> inline const TextureView& get<TextureView>() const {
			if (mType != vk::DescriptorType::eStorageImage && mType != vk::DescriptorType::eSampledImage && mType != vk::DescriptorType::eCombinedImageSampler)
				throw runtime_error("invalid DescriptorType");
			return mTextureView;
		}
		template<> inline const vk::ImageLayout& get<vk::ImageLayout>() const {
			if (mType != vk::DescriptorType::eStorageImage && mType != vk::DescriptorType::eSampledImage && mType != vk::DescriptorType::eCombinedImageSampler)
				throw runtime_error("invalid DescriptorType");
			return mImageLayout;
		}
		template<> inline const shared_ptr<Sampler>& get<shared_ptr<Sampler>>() const {
			if (mType != vk::DescriptorType::eSampler && mType != vk::DescriptorType::eCombinedImageSampler)
				throw runtime_error("invalid DescriptorType");
			return mSampler;
		}
		template<> inline const Buffer::ArrayView& get<Buffer::ArrayView>() const {
			if (mType != vk::DescriptorType::eStorageBuffer && mType != vk::DescriptorType::eUniformBuffer && mType != vk::DescriptorType::eStorageBufferDynamic && mType != vk::DescriptorType::eUniformBufferDynamic)
				throw runtime_error("invalid DescriptorType");
			return mBufferView;
		}
		template<> inline const BufferView& get<BufferView>() const {
			if (mType != vk::DescriptorType::eStorageTexelBuffer && mType != vk::DescriptorType::eUniformTexelBuffer)
				throw runtime_error("invalid DescriptorType");
			return mTexelBufferView;
		}
		template<> inline const byte_blob& get<byte_blob>() const {
			if (mType != vk::DescriptorType::eInlineUniformBlockEXT)
				throw runtime_error("invalid DescriptorType");
			return mInlineUniformData;
		}
		template<> inline const vk::AccelerationStructureKHR& get<vk::AccelerationStructureKHR>() const {
			if (mType != vk::DescriptorType::eAccelerationStructureKHR)
				throw runtime_error("invalid DescriptorType");
			return mAccelerationStructure;
		}
	};

	inline DescriptorSet(Device& device, const string& name, vk::DescriptorSetLayout layout) : DeviceResource(device, name), mLayout(layout) {
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		vk::DescriptorSetAllocateInfo allocInfo = {};
		allocInfo.descriptorPool = *descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;
		mDescriptorSet = mDevice->allocateDescriptorSets(allocInfo)[0];
		mDevice.SetObjectName(mDescriptorSet, mName);
	}
	inline ~DescriptorSet() {
		mBoundDescriptors.clear();
		mPendingWrites.clear();
		auto descriptorPool = mDevice.mDescriptorPool.lock();
		mDevice->freeDescriptorSets(*descriptorPool, { mDescriptorSet });
	}

	inline operator const vk::DescriptorSet*() const { return &mDescriptorSet; }
	inline operator vk::DescriptorSet() const { return mDescriptorSet; }

	inline vk::DescriptorSetLayout Layout() const { return mLayout; }
	inline const string& Name() const { return mName; }

	inline const Entry& at(uint32_t binding, uint32_t arrayIndex = 0) const {
		return mBoundDescriptors.at((uint64_t(binding)<<32)|arrayIndex);
	}

	inline void set(uint32_t binding, Entry&& entry) {
		mBoundDescriptors[*mPendingWrites.emplace(uint64_t(binding)<<32).first] = entry;
	}
	inline void set(uint32_t binding, uint32_t arrayIndex, Entry&& entry) {
		mBoundDescriptors[*mPendingWrites.emplace((uint64_t(binding)<<32)|arrayIndex).first] = entry;
	}

	inline void FlushWrites() {
		if (mPendingWrites.empty()) return;

		struct WriteInfo {
			vk::DescriptorImageInfo  mImageInfo;
			vk::DescriptorBufferInfo mBufferInfo;
			vk::BufferView           mTexelBufferView;
			vk::WriteDescriptorSetInlineUniformBlockEXT mInlineInfo;
			vk::WriteDescriptorSetAccelerationStructureKHR mAccelerationStructureInfo;
		};
		vector<WriteInfo> infos(mPendingWrites.size());
		vector<vk::WriteDescriptorSet> writes(mPendingWrites.size());
		vector<vk::CopyDescriptorSet> copies;
		uint32_t i = 0;
		for (uint64_t idx : mPendingWrites) {
			Entry& entry = mBoundDescriptors.at(idx);
			writes[i].dstSet = mDescriptorSet;
			writes[i].dstBinding = idx >> 32;
			writes[i].dstArrayElement = idx & ~uint32_t(0);
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = entry.mType;
			switch (entry.mType) {
			case vk::DescriptorType::eCombinedImageSampler:
				infos[i].mImageInfo.imageLayout = entry.mImageLayout;
				infos[i].mImageInfo.imageView = *entry.mTextureView;
			case vk::DescriptorType::eSampler:
				infos[i].mImageInfo.sampler = **entry.mSampler;
				writes[i].pImageInfo = &infos[i].mImageInfo;
				break;

			case vk::DescriptorType::eInputAttachment:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
				infos[i].mImageInfo.imageLayout = entry.mImageLayout;
				infos[i].mImageInfo.imageView = *entry.mTextureView;
				writes[i].pImageInfo = &infos[i].mImageInfo;
				break;

			case vk::DescriptorType::eUniformTexelBuffer:
			case vk::DescriptorType::eStorageTexelBuffer:
				infos[i].mTexelBufferView = *entry.mTexelBufferView;
				writes[i].pTexelBufferView = &infos[i].mTexelBufferView;
				break;

			case vk::DescriptorType::eUniformBuffer:
			case vk::DescriptorType::eStorageBuffer:
			case vk::DescriptorType::eUniformBufferDynamic:
			case vk::DescriptorType::eStorageBufferDynamic:
				infos[i].mBufferInfo.buffer = *entry.mBufferView.buffer();
				infos[i].mBufferInfo.offset = entry.mBufferView.offset();
				infos[i].mBufferInfo.range = entry.mBufferView.size_bytes();
				writes[i].pBufferInfo = &infos[i].mBufferInfo;
				break;
			case vk::DescriptorType::eInlineUniformBlockEXT:
				infos[i].mInlineInfo.pData = entry.mInlineUniformData.data();
				infos[i].mInlineInfo.dataSize = (uint32_t)entry.mInlineUniformData.size();
				writes[i].descriptorCount = infos[i].mInlineInfo.dataSize;
				writes[i].pNext = &infos[i].mInlineInfo;
				break;
			case vk::DescriptorType::eAccelerationStructureKHR:
				infos[i].mAccelerationStructureInfo.accelerationStructureCount = 1;
				infos[i].mAccelerationStructureInfo.pAccelerationStructures = &entry.mAccelerationStructure;
				writes[i].pNext = &infos[i].mAccelerationStructureInfo;
				break;
			}
			i++;
		}
		mDevice->updateDescriptorSets(writes, copies);
		mPendingWrites.clear();
	}

private:
	friend class Device;
	friend class CommandBuffer;
	vk::DescriptorSet mDescriptorSet;
	vk::DescriptorSetLayout mLayout;
	
	unordered_map<uint64_t/*{binding,arrayIndex}*/, Entry> mBoundDescriptors;
	unordered_set<uint64_t> mPendingWrites;
};

}