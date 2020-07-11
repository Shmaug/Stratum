#pragma once

#include <Core/RenderPass.hpp>
#include <Core/Window.hpp>
#include <Util/Util.hpp>

class CommandBuffer;
class Texture;

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

	inline uint32_t ColorBufferCount() const { return (uint32_t)mColorFormats.size(); }
	inline VkImageUsageFlags ColorBufferUsage() const { return mColorBufferUsage; }
	inline VkImageUsageFlags DepthBufferUsage() const { return mDepthBufferUsage; }

	inline void Extent(const VkExtent2D& extent) { mExtent = extent; }
	inline void SampleCount(VkSampleCountFlagBits s) { mSampleCount = s; }
	inline void ColorBufferUsage(VkImageUsageFlags usage) { mColorBufferUsage = usage; }
	inline void DepthBufferUsage(VkImageUsageFlags usage) { mDepthBufferUsage = usage; }

	inline VkExtent2D Extent() const { return mExtent; }
	inline VkSampleCountFlagBits SampleCount() const { return mSampleCount; }
	
	inline void ClearValue(uint32_t i, const VkClearValue& value) { mClearValues[i] = value; }
	inline Texture* ColorBuffer(uint32_t i = 0) { return mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()].mColorBuffers[i]; }
	inline Texture* DepthBuffer() { return mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()].mDepthBuffer; }


	ENGINE_EXPORT void Clear(CommandBuffer* commandBuffer, ClearFlags clearFlags = CLEAR_COLOR_DEPTH);
	// Create buffers and RenderPass if necessary
	ENGINE_EXPORT void PreBeginRenderPass();
	// Transition buffers and begin renderpass
	ENGINE_EXPORT void BeginRenderPass(CommandBuffer* commandBuffer);

	// Resolve (or copy, if SampleCount is VK_SAMPLE_COUNT_1_BIT) the color buffer at 'index' to 'destination'
	ENGINE_EXPORT void ResolveColor(CommandBuffer* commandBuffer, uint32_t index, VkImage destination);
	// Resolve (or copy, if SampleCount is VK_SAMPLE_COUNT_1_BIT) the depth buffer to 'destination'
	ENGINE_EXPORT void ResolveDepth(CommandBuffer* commandBuffer, VkImage destination);
	

	inline ::RenderPass* RenderPass() const { return mRenderPass; }
	inline ::Device* Device() const { return mDevice; }

private:
	::Device* mDevice;
	::RenderPass* mRenderPass;

	struct FramebufferData {
		VkFramebuffer mFramebuffer;
		Texture* mDepthBuffer;
		std::vector<Texture*> mColorBuffers;
	};
	std::vector<FramebufferData> mFramebuffers;

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
