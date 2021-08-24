#pragma once

#include "Buffer.hpp"

namespace std {
	
template<>
struct hash<pair<vk::ImageSubresourceRange, vk::ComponentMapping>> {
	inline size_t operator()(const pair<vk::ImageSubresourceRange, vk::ComponentMapping>& v) const {
		return stm::hash_args(v.first, v.second);
	}
};

}
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

class Texture : public DeviceResource {
public:
	static constexpr uint32_t max_mips(const vk::Extent3D& extent) {
		return 32 - (uint32_t)countl_zero(max(max(extent.width, extent.height), extent.depth));
	}

	class View;
	using PixelData = pair<Buffer::TexelView, vk::Extent3D>;
	STRATUM_API static PixelData load(Device& device, const fs::path& filename, bool srgb = true);

	// If mipLevels = 0, will auto-determine according to extent
	inline Texture(shared_ptr<Device::MemoryAllocation> memory, const string& name,
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlags createFlags = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
			: DeviceResource(memory->mDevice, name), mMemory(memory), mExtent(extent), mFormat(format), mArrayLayers(arrayLayers), mSampleCount(numSamples), mUsage(usage), 
			mMipLevels(mipLevels ? mipLevels : (numSamples > vk::SampleCountFlagBits::e1) ? 1 : max_mips(extent)), mCreateFlags(createFlags), mTiling(tiling) {
		create();
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	// If mipLevels = 0, will auto-determine according to extent
	inline Texture(Device& device, const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlags createFlags = {}, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
			: DeviceResource(device, name), mExtent(extent), mFormat(format), mArrayLayers(arrayLayers), mSampleCount(numSamples), mUsage(usage), 
			mMipLevels(mipLevels ? mipLevels : (numSamples > vk::SampleCountFlagBits::e1) ? 1 : max_mips(extent)), mCreateFlags(createFlags), mTiling(tiling) {
		create();
		mMemory = make_shared<Device::MemoryAllocation>(mDevice, mDevice->getImageMemoryRequirements(mImage), memoryUsage);
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	inline Texture(Device& device, const string& name, const vk::Extent3D& extent, const vk::AttachmentDescription& description, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_UNKNOWN, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: DeviceResource(device, name), mExtent(extent), mFormat(description.format), mArrayLayers(1), mSampleCount(description.samples), mUsage(usage), 
			mMipLevels(1), mCreateFlags(createFlags), mTiling(tiling) {
		if (memoryUsage == VMA_MEMORY_USAGE_UNKNOWN)
			memoryUsage = (usage & vk::ImageUsageFlagBits::eTransientAttachment) ? VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED : VMA_MEMORY_USAGE_GPU_ONLY;		
		create();
		mMemory = make_shared<Device::MemoryAllocation>(mDevice, mDevice->getImageMemoryRequirements(mImage), memoryUsage);
		vmaBindImageMemory(mDevice.allocator(), mMemory->allocation(), mImage);
	}

	// Create around vk::Image, but don't own it (for example, swapchain images). Sets mMemory to nullptr
	inline Texture(vk::Image image, Device& device, const string& name, 
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlags createFlags = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: DeviceResource(device, name), mImage(image), mMemory(nullptr), mExtent(extent), mFormat(format), mArrayLayers(arrayLayers), mSampleCount(numSamples), mUsage(usage), mMipLevels(mipLevels), mCreateFlags(createFlags), mTiling(tiling) {
			switch (mFormat) {
			default:
				mAspect = vk::ImageAspectFlagBits::eColor;
				break;
			case vk::Format::eD16Unorm:
			case vk::Format::eD32Sfloat:
				mAspect = vk::ImageAspectFlagBits::eDepth;
				break;
			case vk::Format::eS8Uint:
				mAspect = vk::ImageAspectFlagBits::eStencil;
				break;
			case vk::Format::eD16UnormS8Uint:
			case vk::Format::eD24UnormS8Uint:
			case vk::Format::eD32SfloatS8Uint:
			case vk::Format::eX8D24UnormPack32:
				mAspect = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
				break;
			}
		}
	
	inline ~Texture() {
		for (auto&[k,v] : mViews) mDevice->destroyImageView(v);
		if (mMemory) mDevice->destroyImage(mImage);
	}

	inline vk::Image& operator*() { return mImage; }
	inline vk::Image* operator->() { return &mImage; }
	inline const vk::Image& operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }
	inline operator bool() const { return mImage; }

	inline const auto& memory() const { return mMemory; }
	inline vk::Extent3D extent() const { return mExtent; }
	inline uint32_t mip_levels() const { return mMipLevels; }
	inline uint32_t array_layers() const { return mArrayLayers; }
	inline vk::Format format() const { return mFormat; }
	inline vk::ImageUsageFlags usage() const { return mUsage; }
	inline vk::SampleCountFlags sample_count() const { return mSampleCount; }
	inline vk::ImageAspectFlags aspect() const { return mAspect; }
	inline vk::ImageCreateFlags create_flags() const { return mCreateFlags; }

	// Texture must support vk::ImageLayout::eTransferSrcOptimal and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void generate_mip_maps(CommandBuffer& commandBuffer);
	
	STRATUM_API void transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, const vk::ImageSubresourceRange& subresourceRange, bool force = false);
	inline void transition_barrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout, bool force = false) {
		transition_barrier(commandBuffer, mTrackedStageFlags, guess_stage(newLayout), mTrackedLayout, newLayout, vk::ImageSubresourceRange(mAspect, 0, mip_levels(), 0, array_layers()), force);
	}
	inline void transition_barrier(CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, bool force = false) {
		transition_barrier(commandBuffer, guess_stage(oldLayout), guess_stage(newLayout), oldLayout, newLayout, vk::ImageSubresourceRange(mAspect, 0, mip_levels(), 0, array_layers()), force);
	}
	inline void transition_barrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout, bool force = false) {
		transition_barrier(commandBuffer, mTrackedStageFlags, dstStage, mTrackedLayout, newLayout, vk::ImageSubresourceRange(mAspect, 0, mip_levels(), 0, array_layers()), force);
	}
	
	class View {
	private:
		vk::ImageView mView;
		shared_ptr<Texture> mTexture;
		vk::ImageSubresourceRange mSubresource;
		vk::ComponentMapping mComponents;

	public:
		View() = default;
		View(const View&) = default;
		STRATUM_API View(const shared_ptr<Texture>& texture, const vk::ImageSubresourceRange& subresource, const vk::ComponentMapping& components);
		inline View(const shared_ptr<Texture>& texture, uint32_t baseMip=0, uint32_t mipCount=0, uint32_t baseLayer=0, uint32_t layerCount=0, vk::ImageAspectFlags aspect=(vk::ImageAspectFlags)0, const vk::ComponentMapping& components={})
			: View(texture, vk::ImageSubresourceRange(aspect, baseMip, mipCount, baseLayer, layerCount), components) {};

		View& operator=(const View&) = default;
		View& operator=(View&& v) = default;
		inline bool operator==(const View& rhs) const = default;
		inline operator bool() const { return mTexture && mView; }

		inline const vk::ImageView& operator*() const { return mView; }
		inline const vk::ImageView* operator->() const { return &mView; }
		inline void reset() { mTexture.reset(); }
		inline const auto& texture() const { return mTexture; }
		inline const vk::ImageSubresourceRange& subresource_range() const { return mSubresource; }
		inline const vk::ComponentMapping& components() const { return mComponents; }
		inline vk::ImageSubresourceLayers subresource(uint32_t level) const {
			return vk::ImageSubresourceLayers(mSubresource.aspectMask, mSubresource.baseMipLevel + level, mSubresource.baseArrayLayer, mSubresource.layerCount);
		}
	
		inline void transition_barrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout, bool force = false) const {
			mTexture->transition_barrier(commandBuffer, mTexture->mTrackedStageFlags, guess_stage(newLayout), mTexture->mTrackedLayout, newLayout, mSubresource, force);
		}
	};

private:
	friend class CommandBuffer;
	friend class Texture::View;
	friend class stm::Window;

	STRATUM_API void create();

	vk::Image mImage;
	shared_ptr<Device::MemoryAllocation> mMemory;
	
	vk::Extent3D mExtent;
	uint32_t mArrayLayers = 0;
	vk::Format mFormat;
	uint32_t mMipLevels = 0;
	vk::SampleCountFlagBits mSampleCount;
	vk::ImageUsageFlags mUsage;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageAspectFlags mAspect;
	vk::ImageTiling mTiling = vk::ImageTiling::eOptimal;
	
	unordered_map<pair<vk::ImageSubresourceRange, vk::ComponentMapping>, vk::ImageView> mViews;
	
	vk::ImageLayout mTrackedLayout = vk::ImageLayout::eUndefined;
	vk::PipelineStageFlags mTrackedStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
	vk::AccessFlags mTrackedAccessFlags = {};
};

}

namespace std {
	
template<>
struct hash<stm::Texture::View> {
	inline size_t operator()(const stm::Texture::View& v) const {
		return stm::hash_args(v.texture().get(), v.subresource_range(), v.components());
	}
};

}