#include <cmath>

#include <Content/AssetManager.hpp>
#include <Content/Shader.hpp>
#include <Content/Texture.hpp>

#include <Core/Buffer.hpp>
#include <Core/CommandBuffer.hpp>
#include <Util/Util.hpp>
#include <ThirdParty/stb_image.h>

using namespace std;

Sampler::Sampler(const string& name, Device* device, const VkSamplerCreateInfo& samplerInfo) : mName(name), mDevice(device) {
	ThrowIfFailed(vkCreateSampler(*mDevice, &samplerInfo, nullptr, &mSampler), "vkCreateSampler failed");
	mDevice->SetObjectName(mSampler, mName, VK_OBJECT_TYPE_SAMPLER);
}
Sampler::Sampler(const string& name, Device* device, float maxLod, VkFilter filter, VkSamplerAddressMode addressMode, float maxAnisotropy)
	: mName(name), mDevice(device) {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.anisotropyEnable = maxAnisotropy > 0 ? VK_TRUE : VK_FALSE;
	samplerInfo.maxAnisotropy = maxAnisotropy;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = (float)maxLod;
	samplerInfo.mipLodBias = 0;

	ThrowIfFailed(vkCreateSampler(*mDevice, &samplerInfo, nullptr, &mSampler), "vkCreateSampler failed");
	mDevice->SetObjectName(mSampler, mName, VK_OBJECT_TYPE_SAMPLER);
}
Sampler::~Sampler() {
	vkDestroySampler(*mDevice, mSampler, nullptr);
}


uint8_t* load(const string& filename, bool srgb, uint32_t& pixelSize, int32_t& x, int32_t& y, int32_t& channels, VkFormat& format) {
	uint8_t* pixels = nullptr;
	pixelSize = 0;
	x, y, channels;
	stbi_info(filename.c_str(), &x, &y, &channels);

	int desiredChannels = 4;
	if (stbi_is_16_bit(filename.c_str())) {
		pixels = (uint8_t*)stbi_load_16(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint16_t);
		srgb = false;
	} else if (stbi_is_hdr(filename.c_str())) {
		pixels = (uint8_t*)stbi_loadf(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(float);
		srgb = false;
	} else {
		pixels = (uint8_t*)stbi_load(filename.c_str(), &x, &y, &channels, desiredChannels);
		pixelSize = sizeof(uint8_t);
	}
	if (!pixels) {
		fprintf_color(COLOR_RED, stderr, "Failed to load image: %s\n", filename.c_str());
		throw;
	}
	if (desiredChannels > 0) channels = desiredChannels;

	if (srgb) {
		const VkFormat formatMap[4] {
			VK_FORMAT_R8_SRGB, VK_FORMAT_R8G8_SRGB, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
		};
		format = formatMap[channels - 1];
	} else {
		const VkFormat formatMap[4][4]{
			{ VK_FORMAT_R8_UNORM  , VK_FORMAT_R8G8_UNORM   , VK_FORMAT_R8G8B8_UNORM    , VK_FORMAT_R8G8B8A8_UNORM      },
			{ VK_FORMAT_R16_UNORM , VK_FORMAT_R16G16_UNORM , VK_FORMAT_R16G16B16_UNORM , VK_FORMAT_R16G16B16A16_UNORM  },
			{ VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT },
			{ VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT },
		};
		format = formatMap[pixelSize - 1][channels - 1];
	}

	return pixels;
}


Texture::Texture(const string& name, Device* device, const string& filename, bool srgb) : mName(name), mDevice(device), mMemory({}), mTiling(VK_IMAGE_TILING_OPTIMAL) {
	int32_t x, y, channels;
	uint32_t size;
	uint8_t* pixels = load(filename, srgb, size, x, y, channels, mFormat);

	mExtent = { (uint32_t)x, (uint32_t)y, 1 };
	mArrayLayers = 1;
	mMipLevels = (uint32_t)std::floor(std::log2(std::max(mExtent.width, mExtent.height))) + 1;
	mSampleCount = VK_SAMPLE_COUNT_1_BIT;
	mUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	mMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	Create();

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = mAspectFlags;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = 1;
	copyRegion.imageOffset = { 0, 0, 0 };
	copyRegion.imageExtent = mExtent;

	VkDeviceSize dataSize = mExtent.width * mExtent.height * size * channels;

	CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();
	Buffer* uploadBuffer = commandBuffer->GetBuffer(name + " Copy", dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	uploadBuffer->Upload(pixels, dataSize);

	commandBuffer->TransitionBarrier(this, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkCmdCopyBufferToImage(*commandBuffer, *uploadBuffer, mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
	GenerateMipMaps(commandBuffer);
	mDevice->Execute(commandBuffer);
	stbi_image_free(pixels);

	mLastKnownLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	
	//printf("Loaded %s: %dx%d %s\n", filename.c_str(), mExtent.width, mExtent.height, FormatToString(mFormat));
}
Texture::Texture(const string& name, Device* device, const string& px, const string& nx, const string& py, const string& ny, const string& pz, const string& nz, bool srgb)
	: mName(name), mDevice(device), mMemory({}), mTiling(VK_IMAGE_TILING_OPTIMAL) {
	int32_t x, y, channels;
	uint32_t size;
	
	uint8_t* pixels[6] {
		load(px, srgb, size, x, y, channels, mFormat),
		load(nx, srgb, size, x, y, channels, mFormat),
		load(py, srgb, size, x, y, channels, mFormat),
		load(ny, srgb, size, x, y, channels, mFormat),
		load(pz, srgb, size, x, y, channels, mFormat),
		load(nz, srgb, size, x, y, channels, mFormat)
	};

	mExtent = { (uint32_t)x, (uint32_t)y, 1 };
	mArrayLayers = 6;
	mMipLevels = (uint32_t)std::floor(std::log2(std::max(mExtent.width, mExtent.height))) + 1;
	mSampleCount = VK_SAMPLE_COUNT_1_BIT;
	mUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	mMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	Create();

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = mAspectFlags;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = mArrayLayers;
	copyRegion.imageOffset = { 0, 0, 0 };
	copyRegion.imageExtent = { mExtent.width, mExtent.height, 1 };

	VkDeviceSize dataSize = mExtent.width * mExtent.height * size * channels;

	CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();
	Buffer* uploadBuffer = commandBuffer->GetBuffer(name + " Copy", dataSize * mArrayLayers, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	for (uint32_t j = 0; j < mArrayLayers; j++)
		memcpy((uint8_t*)uploadBuffer->MappedData() + j * dataSize, pixels[j], dataSize);

	commandBuffer->TransitionBarrier(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkCmdCopyBufferToImage(*commandBuffer, *uploadBuffer, mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
	GenerateMipMaps(commandBuffer);
	mDevice->Execute(commandBuffer);

	for (uint32_t i = 0; i < 6; i++)
		stbi_image_free(pixels[i]);

	mLastKnownLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//printf("Loaded Cubemap %s: %dx%d %s\n", nx.c_str(), mExtent.width, mExtent.height, FormatToString(mFormat));
}


Texture::Texture(const string& name, Device* device, const void* data, VkDeviceSize dataSize,
		const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
		VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
		: mName(name), mDevice(device), mExtent(extent), mArrayLayers(1), mMipLevels(mipLevels), mFormat(format), mSampleCount(numSamples), mUsage(usage), mMemoryProperties(properties), mMemory({}), mTiling(VK_IMAGE_TILING_OPTIMAL) {
	
	if (mipLevels == 0) mMipLevels = (uint32_t)std::floor(std::log2(std::max(mExtent.width, mExtent.height))) + 1;

	if (dataSize) {
		mUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (mMipLevels > 1) mUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		Create();

		Buffer uploadBuffer(name + " Copy", mDevice, data, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = mAspectFlags;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageOffset = { 0, 0, 0 };
		copyRegion.imageExtent = mExtent;
		CommandBuffer* commandBuffer = mDevice->GetCommandBuffer();
		commandBuffer->TransitionBarrier(this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		vkCmdCopyBufferToImage(*commandBuffer, uploadBuffer, mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		if (mMipLevels > 1) GenerateMipMaps(commandBuffer);
		mDevice->Execute(commandBuffer);
		commandBuffer->Wait();
	} else
		Create();
}

Texture::Texture(const string& name, Device* device, 
	const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
	: Texture(name, device, (const void*)nullptr, 0, extent, format, mipLevels, numSamples, usage, properties) {}
Texture::Texture(const string& name, Device* device, const void* data, VkDeviceSize dataSize,
	const VkExtent2D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
	: Texture(name, device, data, dataSize, { extent.width, extent.height, 1 }, format, mipLevels, numSamples, usage, properties) {}
Texture::Texture(const string& name, Device* device, 
	const VkExtent2D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
	: Texture(name, device, (const void*)nullptr, 0, { extent.width, extent.height, 1 }, format, mipLevels, numSamples, usage, properties) {}

Texture::~Texture() {
	for (auto& kp : mMipViews)
		if (kp.second != VK_NULL_HANDLE)
			vkDestroyImageView(*mDevice, kp.second, nullptr);
	vkDestroyImageView(*mDevice, mView, nullptr);
	vkDestroyImage(*mDevice, mImage, nullptr);
	mDevice->FreeMemory(mMemory);
}

VkImageView Texture::View(uint32_t mipLevel) {
	if (mipLevel == 0 && mMipLevels == 1) return mView;

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.viewType = mArrayLayers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : (mExtent.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : (mExtent.height > 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D));
	if (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1) viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // special 1x1 case
	viewInfo.image = mImage;
	viewInfo.format = mFormat;
	viewInfo.subresourceRange.aspectMask = mAspectFlags;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = mArrayLayers;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mMipLevels;
	viewInfo.subresourceRange.baseMipLevel = mipLevel;
	viewInfo.subresourceRange.levelCount = 1;
	VkImageView view;
	ThrowIfFailed(vkCreateImageView(*mDevice, &viewInfo, nullptr, &view), "vkCreateImageView failed for " + mName + "[" + to_string(mipLevel) + "]");
	mDevice->SetObjectName(view, mName + " View[" + to_string(mipLevel) + "]", VK_OBJECT_TYPE_IMAGE_VIEW);
	mMipViews[mipLevel] = view;
}

void Texture::Create() {
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = mExtent.depth > 1 ? VK_IMAGE_TYPE_3D : (mExtent.height > 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D);
	if (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1) imageInfo.imageType = VK_IMAGE_TYPE_2D; // special 1x1 case
	imageInfo.extent.width = mExtent.width;
	imageInfo.extent.height = mExtent.height;
	imageInfo.extent.depth = mExtent.depth;
	imageInfo.mipLevels = mMipLevels;
	imageInfo.arrayLayers = mArrayLayers;
	imageInfo.format = mFormat;
	imageInfo.tiling = mTiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = mUsage;
	imageInfo.samples = mSampleCount;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.flags = mArrayLayers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
	ThrowIfFailed(vkCreateImage(*mDevice, &imageInfo, nullptr, &mImage), "vkCreateImage failed for " + mName);
	mDevice->SetObjectName(mImage, mName, VK_OBJECT_TYPE_IMAGE);

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(*mDevice, mImage, &memRequirements);
	mMemory = mDevice->AllocateMemory(memRequirements, mMemoryProperties, mName);
	vkBindImageMemory(*mDevice, mImage, mMemory.mDeviceMemory, mMemory.mOffset);
	
	mAspectFlags = 0;
	switch (mFormat) {
	default:
		mAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		break;
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
		mAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
		break;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		mAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		break;
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.viewType = mArrayLayers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : (mExtent.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : (mExtent.height > 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D));
	if (mExtent.width == 1 && mExtent.height == 1 && mExtent.depth == 1) viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // special 1x1 case
	viewInfo.image = mImage;
	viewInfo.format = mFormat;
	viewInfo.subresourceRange.aspectMask = mAspectFlags;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = mArrayLayers;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mMipLevels;
	ThrowIfFailed(vkCreateImageView(*mDevice, &viewInfo, nullptr, &mView), "vkCreateImageView failed for " + mName);
	mDevice->SetObjectName(mView, mName + " View", VK_OBJECT_TYPE_IMAGE_VIEW);

	mLastKnownLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	mLastKnownStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	mLastKnownAccessFlags = 0;
}

void Texture::GenerateMipMaps(CommandBuffer* commandBuffer) {
	// Transition mip 0 to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	commandBuffer->TransitionBarrier(*this, { mAspectFlags, 0, 1, 0, mArrayLayers }, mLastKnownStageFlags, VK_PIPELINE_STAGE_TRANSFER_BIT, mLastKnownLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	// Transition all other mips to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	commandBuffer->TransitionBarrier(*this, { mAspectFlags, 1, mMipLevels - 1, 0, mArrayLayers }, mLastKnownStageFlags, VK_PIPELINE_STAGE_TRANSFER_BIT, mLastKnownLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkImageBlit blit = {};
	blit.srcOffsets[0] = { 0, 0, 0 };
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = mArrayLayers;
	blit.srcSubresource.aspectMask = mAspectFlags;
	blit.dstOffsets[0] = { 0, 0, 0 };
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = mArrayLayers;
	blit.dstSubresource.aspectMask = mAspectFlags;

	blit.srcOffsets[1] = { (int32_t)mExtent.width, (int32_t)mExtent.height, (int32_t)mExtent.depth };

	for (uint32_t i = 1; i < mMipLevels; i++) {
		// Blit the previous mip level into this one
		blit.srcSubresource.mipLevel = i - 1;
		blit.dstSubresource.mipLevel = i;
		blit.dstOffsets[1].x = max(1, blit.srcOffsets[1].x / 2);
		blit.dstOffsets[1].y = max(1, blit.srcOffsets[1].y / 2);
		blit.dstOffsets[1].z = max(1, blit.srcOffsets[1].z / 2);
		vkCmdBlitImage(*commandBuffer,
			mImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		blit.srcOffsets[1] = blit.dstOffsets[1];
		
		// Transition this mip level to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
		commandBuffer->TransitionBarrier(*this, { mAspectFlags, i, 1, 0, mArrayLayers }, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}

	mLastKnownLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	mLastKnownStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	mLastKnownAccessFlags = VK_ACCESS_TRANSFER_READ_BIT;
}