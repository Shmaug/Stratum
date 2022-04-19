#pragma once

#include "Buffer.hpp"

namespace stm {

class Sampler : public DeviceResource {
private:
	vk::Sampler mSampler;
	vk::SamplerCreateInfo mInfo;

public:
	inline Sampler(Device& device, const string& name, const vk::SamplerCreateInfo& samplerInfo) : DeviceResource(device, name), mInfo(samplerInfo) {
		mSampler = mDevice->createSampler(mInfo);
		mDevice.set_debug_name(mSampler, name);
	}
	inline ~Sampler() {
		mDevice->destroySampler(mSampler);
	}

	inline vk::Sampler& operator*() { return mSampler; }
	inline vk::Sampler* operator->() { return &mSampler; }
	inline const vk::Sampler& operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }

	inline const vk::SamplerCreateInfo& create_info() const { return mInfo; }
};

struct ImageData {
	Buffer::TexelView pixels;
	vk::Extent3D extent;
};
STRATUM_API ImageData load_image_data(Device& device, const fs::path& filename, bool srgb = true, int desiredChannels = 0);

class Image : public DeviceResource {
public:
	static constexpr uint32_t max_mips(const vk::Extent3D& extent) {
		return 32 - (uint32_t)countl_zero(max(max(extent.width, extent.height), extent.depth));
	}

	class View;
	// If mipLevels = 0, will auto-determine according to extent
	inline Image(shared_ptr<Device::MemoryAllocation> memory, const string& name,
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlags createFlags = {}, vk::ImageType type = (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
			: DeviceResource(memory->mDevice, name), mMemory(memory), mExtent(extent), mFormat(format), mLayerCount(arrayLayers), mSampleCount(numSamples), mUsage(usage), 
			mLevelCount(mipLevels ? mipLevels : (numSamples > vk::SampleCountFlagBits::e1) ? 1 : max_mips(extent)), mCreateFlags(createFlags), mType(type != (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM ? type : mExtent.depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D), mTiling(tiling) {
		init_state();
		create();
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	// If mipLevels = 0, will auto-determine according to extent. always generates mipmaps.
	STRATUM_API Image(CommandBuffer& commandBuffer, const string& name, const ImageData& pixels, uint32_t levelCount = 0, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY, vk::ImageTiling tiling = vk::ImageTiling::eOptimal);

	// If mipLevels = 0, will auto-determine according to extent
	inline Image(Device& device, const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY, vk::ImageCreateFlags createFlags = {}, vk::ImageType type = (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
			: DeviceResource(device, name), mExtent(extent), mFormat(format), mLayerCount(arrayLayers), mSampleCount(numSamples), mUsage(usage), 
			mLevelCount(mipLevels ? mipLevels : (numSamples > vk::SampleCountFlagBits::e1) ? 1 : max_mips(extent)), mCreateFlags(createFlags), mType(type != (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM ? type : mExtent.depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D), mTiling(tiling) {
		init_state();
		create();
		mMemory = make_shared<Device::MemoryAllocation>(mDevice, mDevice->getImageMemoryRequirements(mImage), memoryUsage);
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	inline Image(Device& device, const string& name, const vk::Extent3D& extent, const vk::AttachmentDescription& description, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_UNKNOWN, vk::ImageCreateFlags createFlags = {}, vk::ImageType type = (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: DeviceResource(device, name), mExtent(extent), mFormat(description.format), mLayerCount(1), mSampleCount(description.samples), mUsage(usage), 
			mLevelCount(1), mCreateFlags(createFlags), mType(type != (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM ? type : mExtent.depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D), mTiling(tiling) {
		if (memoryUsage == VMA_MEMORY_USAGE_UNKNOWN)
			memoryUsage = (usage & vk::ImageUsageFlagBits::eTransientAttachment) ? VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED : VMA_MEMORY_USAGE_GPU_ONLY;
		init_state();
		create();
		mMemory = make_shared<Device::MemoryAllocation>(mDevice, mDevice->getImageMemoryRequirements(mImage), memoryUsage);
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	// Create around vk::Image, but don't own it (for example, swapchain images). Sets mMemory to nullptr
	inline Image(vk::Image image, Device& device, const string& name, 
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageType type = (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: DeviceResource(device, name), mImage(image), mMemory(nullptr), mExtent(extent), mFormat(format), mLayerCount(arrayLayers), mSampleCount(numSamples), mUsage(usage),
			mLevelCount(mipLevels), mType(type != (vk::ImageType)VK_IMAGE_TYPE_MAX_ENUM ? type : mExtent.depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D), mTiling(tiling) {
		init_state();
	}
	
	inline ~Image() {
		for (auto&[k,v] : mViews) mDevice->destroyImageView(v);
		if (mMemory) mDevice->destroyImage(mImage);
	}

	inline vk::Image& operator*() { return mImage; }
	inline vk::Image* operator->() { return &mImage; }
	inline const vk::Image& operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }
	inline operator bool() const { return mImage; }

	inline const auto& memory() const { return mMemory; }
	inline const vk::Extent3D& extent() const { return mExtent; }
	inline const vk::Format& format() const { return mFormat; }
	inline const vk::SampleCountFlagBits& sample_count() const { return mSampleCount; }
	inline const uint32_t& level_count() const { return mLevelCount; }
	inline const uint32_t& layer_count() const { return mLayerCount; }
	inline const vk::ImageAspectFlags& aspect() const { return mAspect; }
	inline const vk::ImageUsageFlags& usage() const { return mUsage; }
	inline const vk::ImageCreateFlags& create_flags() const { return mCreateFlags; }
	inline const vk::ImageType& type() const { return mType; }

	// Image must support vk::ImageLayout::eTransferSrcOptimal and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void generate_mip_maps(CommandBuffer& commandBuffer);
	
	STRATUM_API void transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout, vk::AccessFlags accessFlag, vk::ImageSubresourceRange subresourceRange = {});
	inline void transition_barrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout, vk::ImageSubresourceRange subresourceRange = {}) {
		transition_barrier(commandBuffer, guess_stage(newLayout), newLayout, guess_access_flags(newLayout), subresourceRange);
	}
	
	class View {
	private:
		vk::ImageView mView;
		shared_ptr<Image> mImage;
		vk::ImageSubresourceRange mSubresource;
		vk::ComponentMapping mComponents;

	public:
		View() = default;
		View(const View&) = default;
		STRATUM_API View(const shared_ptr<Image>& image, const vk::ImageSubresourceRange& subresource, const vk::ComponentMapping& components = {}, vk::ImageViewType type = (vk::ImageViewType)VK_IMAGE_VIEW_TYPE_MAX_ENUM);
		inline View(const shared_ptr<Image>& image, uint32_t baseMip=0, uint32_t mipCount=0, uint32_t baseLayer=0, uint32_t layerCount=0, vk::ImageAspectFlags aspect=(vk::ImageAspectFlags)0, const vk::ComponentMapping& components={}, vk::ImageViewType type = (vk::ImageViewType)VK_IMAGE_VIEW_TYPE_MAX_ENUM)
			: View(image, vk::ImageSubresourceRange(aspect, baseMip, mipCount, baseLayer, layerCount), components, type) {};

		View& operator=(const View&) = default;
		View& operator=(View&& v) = default;
		inline bool operator==(const View& rhs) const = default;
		inline operator bool() const { return mImage && mView; }

		inline const vk::ImageView& operator*() const { return mView; }
		inline const vk::ImageView* operator->() const { return &mView; }
		inline void reset() { mImage.reset(); }
		inline const auto& image() const { return mImage; }
		inline const vk::ImageSubresourceRange& subresource_range() const { return mSubresource; }
		inline const vk::ComponentMapping& components() const { return mComponents; }
		inline vk::ImageSubresourceLayers subresource(uint32_t level) const {
			return vk::ImageSubresourceLayers(mSubresource.aspectMask, mSubresource.baseMipLevel + level, mSubresource.baseArrayLayer, mSubresource.layerCount);
		}
		inline vk::Extent3D extent(uint32_t level = 0) const {
			uint32_t s = 1 << (mSubresource.baseMipLevel + level);
			const vk::Extent3D& e = mImage->extent();
			return vk::Extent3D(max(e.width / s, 1u), max(e.height / s, 1u), max(e.depth / s, 1u));
		}
	
		inline void transition_barrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout) const {
			mImage->transition_barrier(commandBuffer, guess_stage(newLayout), newLayout, guess_access_flags(newLayout), mSubresource);
		}
		inline void transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout, vk::AccessFlags accessFlags) const {
			mImage->transition_barrier(commandBuffer, dstStage, newLayout, accessFlags, mSubresource);
		}
	};

private:
	friend class CommandBuffer;
	friend class View;
	friend class stm::Window;

	STRATUM_API void create();
	void init_state();

	vk::Image mImage;
	shared_ptr<Device::MemoryAllocation> mMemory;
	
	vk::Extent3D mExtent;
	vk::Format mFormat;
	vk::SampleCountFlagBits mSampleCount;
	uint32_t mLayerCount; // array layers
	uint32_t mLevelCount; // mip levels
	vk::ImageAspectFlags mAspect;
	vk::ImageUsageFlags mUsage;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageType mType;
	vk::ImageTiling mTiling;
	
	unordered_map<pair<vk::ImageSubresourceRange, vk::ComponentMapping>, vk::ImageView> mViews;
	
	unordered_map<size_t, tuple<vk::ImageLayout, vk::PipelineStageFlags, vk::AccessFlags>> mTrackedState;
	inline tuple<vk::ImageLayout, vk::PipelineStageFlags, vk::AccessFlags>& tracked_state(vk::ImageAspectFlags aspect, uint32_t layer, uint32_t level) {
		return mTrackedState[hash_args(aspect, layer, level)];
	}
};

}

namespace std {

template<>
struct hash<stm::Image::View> {
	inline size_t operator()(const stm::Image::View& v) const {
		return stm::hash_args(v.image().get(), v.subresource_range(), v.components());
	}
};

}