#pragma once

#include <Core/Device.hpp>

class Framebuffer {
public:
	const std::string mName;

	ENGINE_EXPORT Framebuffer(const std::string& name, ::Device* device, const VkExtent2D& extent,
		const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
		VkSampleCountFlagBits sampleCount,
		VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, const std::vector<VkSubpassDependency>& dependencies = {});
	ENGINE_EXPORT ~Framebuffer();
	
	inline ::Device* Device() const { return mDevice; }

	inline VkExtent2D Extent() const { return mExtent; }
	inline VkSampleCountFlagBits SampleCount() const { return mSampleCount; }
	inline uint32_t ColorBufferCount() const { return (uint32_t)mColorFormats.size(); }
	inline VkImageUsageFlags ColorBufferUsage() const { return mColorBufferUsage; }
	inline VkImageUsageFlags DepthBufferUsage() const { return mDepthBufferUsage; }
	
	inline const std::vector<VkClearValue>& ClearValues() const { return mClearValues; }
	inline void ClearValue(uint32_t i, const VkClearValue& value) { mClearValues[i] = value; }

	ENGINE_EXPORT void Clear(CommandBuffer* commandBuffer, ClearFlags clearFlags = CLEAR_COLOR_DEPTH);

	// Resolve (or copy, if SampleCount is VK_SAMPLE_COUNT_1_BIT) the color buffer at 'index' to 'destination'
	ENGINE_EXPORT void ResolveColor(CommandBuffer* commandBuffer, uint32_t index, VkImage destination);
	// Resolve (or copy, if SampleCount is VK_SAMPLE_COUNT_1_BIT) the depth buffer to 'destination'
	ENGINE_EXPORT void ResolveDepth(CommandBuffer* commandBuffer, VkImage destination);
	
	inline Texture* ColorBuffer(uint32_t i = 0) { return mColorBuffers[i]; }
	inline Texture* DepthBuffer() { return mDepthBuffer; }

	inline operator VkFramebuffer() const { return mFramebuffer; };

private:
	::Device* mDevice;
	::RenderPass* mRenderPass;

	VkFramebuffer mFramebuffer;
	Texture* mDepthBuffer;
	std::vector<Texture*> mColorBuffers;

	std::vector<VkSubpassDependency> mSubpassDependencies;
	VkAttachmentLoadOp mLoadOp;

	VkExtent2D mExtent;
	VkSampleCountFlagBits mSampleCount;
	VkImageUsageFlags mColorBufferUsage;
	VkImageUsageFlags mDepthBufferUsage;
	std::vector<VkFormat> mColorFormats;
	std::vector<VkClearValue> mClearValues;
	VkFormat mDepthFormat;

	ENGINE_EXPORT void CreateRenderPass();
	ENGINE_EXPORT void Create();
};
