#pragma once

#include "Buffer.hpp"

namespace stm {

// TODO: Texture::View object for Swapchain images, to allow for the swapchain to be used directly in a renderpass

inline vk::AccessFlags GuessAccessMask(vk::ImageLayout layout) {
	switch (layout) {
    case vk::ImageLayout::eUndefined:
    case vk::ImageLayout::ePresentSrcKHR:
    case vk::ImageLayout::eColorAttachmentOptimal:
			return {};

    case vk::ImageLayout::eGeneral:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    case vk::ImageLayout::eDepthAttachmentOptimal:
    case vk::ImageLayout::eStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    case vk::ImageLayout::eDepthReadOnlyOptimal:
    case vk::ImageLayout::eStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead;
		
    case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
	}
	return vk::AccessFlagBits::eShaderRead;
}
inline vk::PipelineStageFlags GuessStage(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eGeneral:
			return vk::PipelineStageFlagBits::eComputeShader;

		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::PipelineStageFlagBits::eColorAttachmentOutput;
		
		case vk::ImageLayout::eShaderReadOnlyOptimal:
		case vk::ImageLayout::eDepthReadOnlyOptimal:
		case vk::ImageLayout::eStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::PipelineStageFlagBits::eFragmentShader;

		case vk::ImageLayout::eTransferSrcOptimal:
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::PipelineStageFlagBits::eTransfer;

		case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		case vk::ImageLayout::eStencilAttachmentOptimal:
		case vk::ImageLayout::eDepthAttachmentOptimal:
		case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
			return vk::PipelineStageFlagBits::eLateFragmentTests;

		case vk::ImageLayout::ePresentSrcKHR:
		case vk::ImageLayout::eSharedPresentKHR:
			return vk::PipelineStageFlagBits::eBottomOfPipe;

		default:
			return vk::PipelineStageFlagBits::eTopOfPipe;
	}
}

class Texture : public DeviceResource {
public:
	static constexpr uint32_t MaxMips(const vk::Extent3D& extent) {
		return 32 - (uint32_t)countl_zero(max(max(extent.width, extent.height), extent.depth));
	}

	class View;
	using PixelData = tuple<shared_ptr<Device::Memory::View>, vk::Extent3D, vk::Format>;
	STRATUM_API static PixelData LoadPixels(Device& device, const fs::path& filename, bool srgb = true);

	// If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(shared_ptr<Device::Memory::View> memory, const string& name,
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0,
		vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, 
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal);

	// If mipLevels = 0, will auto-determine according to extent 
	STRATUM_API Texture(Device& device, const string& name,
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0,
		vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1, 
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageTiling tiling = vk::ImageTiling::eOptimal);
	
	// Create around vk::Image, but don't own it
	inline Texture(vk::Image image, Device& device, const string& name, 
		const vk::Extent3D& extent, vk::Format format, uint32_t arrayLayers = 1, uint32_t mipLevels = 0, vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1,
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlags createFlags = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: DeviceResource(device, name), mImage(image), mMemory(nullptr), mExtent(extent), mFormat(format), mArrayLayers(arrayLayers), mSampleCount(numSamples), mUsage(usage), mMipLevels(mipLevels), mCreateFlags(createFlags), mTiling(tiling) {
			switch (mFormat) {
			default:
				mAspectFlags = vk::ImageAspectFlagBits::eColor;
				break;
			case vk::Format::eD16Unorm:
			case vk::Format::eD32Sfloat:
				mAspectFlags = vk::ImageAspectFlagBits::eDepth;
				break;
			case vk::Format::eS8Uint:
				mAspectFlags = vk::ImageAspectFlagBits::eStencil;
				break;
			case vk::Format::eD16UnormS8Uint:
			case vk::Format::eD24UnormS8Uint:
			case vk::Format::eD32SfloatS8Uint:
			case vk::Format::eX8D24UnormPack32:
				mAspectFlags = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
				break;
			}
		}
	
	
	inline Texture(shared_ptr<Device::Memory::View> memory, const string& name, const vk::Extent3D& extent, const vk::AttachmentDescription& description, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: Texture(memory, name, extent, description.format, 1, 1, description.samples, usage, createFlags, tiling) {}
	
	inline Texture(Device& device, const string& name, const vk::Extent3D& extent, const vk::AttachmentDescription& description, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled, 
		vk::ImageCreateFlags createFlags = {}, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageTiling tiling = vk::ImageTiling::eOptimal)
		: Texture(device, name, extent, description.format, 1, 1, description.samples, usage, createFlags, memoryProperties, tiling) {}

	inline ~Texture() {
		for (auto&[k,v] : mViews) mDevice->destroyImageView(v);
		if (mMemory) mDevice->destroyImage(mImage); // if mMemory isn't set, then the image object isn't owned by this object (ie swapchain images)
	}

	inline const vk::Image& operator*() const { return mImage; }
	inline const vk::Image* operator->() const { return &mImage; }
	inline operator bool() const { return mImage; }

	inline vk::Extent3D Extent() const { return mExtent; }
	inline vk::Format Format() const { return mFormat; }
	inline vk::ImageUsageFlags Usage() const { return mUsage; }
	inline vk::SampleCountFlags SampleCount() const { return mSampleCount; }
	inline uint32_t MipLevels() const { return mMipLevels; }
	inline uint32_t ArrayLayers() const { return mArrayLayers; }
	inline vk::ImageAspectFlags AspectFlags() const { return mAspectFlags; }
	inline const Device::Memory::View& Memory() const { return *mMemory; }
	inline vk::ImageCreateFlags CreateFlags() const { return mCreateFlags; }

	// Texture must support vk::ImageAspect::eColor and vk::ImageLayout::eTransferDstOptimal
	STRATUM_API void GenerateMipMaps(CommandBuffer& commandBuffer);
	
	STRATUM_API void TransitionBarrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, mTrackedStageFlags, GuessStage(newLayout), mTrackedLayout, newLayout);
	}
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
	}
	inline void TransitionBarrier(CommandBuffer& commandBuffer, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout) {
		TransitionBarrier(commandBuffer, mTrackedStageFlags, dstStage, mTrackedLayout, newLayout);
	}
	
	class View {
		vk::ImageView mView;
		shared_ptr<Texture> mTexture;
		vk::ImageAspectFlags mAspectMask;
		uint32_t mBaseMip;
		uint32_t mMipCount;
		uint32_t mBaseLayer;
		uint32_t mLayerCount;

	public:
		View() = default;
		View(const View&) = default;
		View(View&&) = default;
		STRATUM_API View(shared_ptr<Texture> texture, uint32_t baseMip=0, uint32_t mipCount=0, uint32_t baseLayer=0, uint32_t layerCount=0, vk::ImageAspectFlags aspectMask=(vk::ImageAspectFlags)0, vk::ComponentMapping components={});

		View& operator=(const View&) = default;
		View& operator=(View&& v) = default;
		inline bool operator==(const View& rhs) const = default;
		inline operator bool() const { return mTexture && mView; }

		inline const vk::ImageView& operator*() const { return mView; }
		inline const vk::ImageView* operator->() const { return &mView; }
		inline shared_ptr<Texture> get() const { return mTexture; }
		inline Texture& texture() const { return *mTexture; }
	};

private:
	friend class CommandBuffer;
	friend class Texture::View;
	friend class Window;

	vk::Image mImage;
	shared_ptr<Device::Memory::View> mMemory;
	
	vk::Extent3D mExtent;
	uint32_t mArrayLayers = 0;
	vk::Format mFormat;
	uint32_t mMipLevels = 0;
	vk::SampleCountFlagBits mSampleCount;
	vk::ImageUsageFlags mUsage;
	vk::ImageCreateFlags mCreateFlags;
	vk::ImageAspectFlags mAspectFlags;
	vk::ImageTiling mTiling = vk::ImageTiling::eOptimal;
	
	unordered_map<size_t, vk::ImageView> mViews;
	
	vk::ImageLayout mTrackedLayout = vk::ImageLayout::eUndefined;
	vk::PipelineStageFlags mTrackedStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
	vk::AccessFlags mTrackedAccessFlags = {};

	STRATUM_API void Create();
};
}