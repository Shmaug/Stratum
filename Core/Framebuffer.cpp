#include <Core/Framebuffer.hpp>
#include <Core/RenderPass.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Window.hpp>
#include <Content/Texture.hpp>
#include <Util/Profiler.hpp>

using namespace std;

Framebuffer::Framebuffer(const string& name, ::Device* device, const VkExtent2D& extent, 
	const vector<VkFormat>& colorFormats, VkFormat depthFormat,
	VkSampleCountFlagBits sampleCount,
	VkImageUsageFlags colorUsage, VkImageUsageFlags depthUsage,
	VkAttachmentLoadOp loadOp, const vector<VkSubpassDependency>& dependencies)
	: mName(name), mDevice(device), mRenderPass(nullptr),
	mExtent(extent), mSampleCount(sampleCount), mColorFormats(colorFormats), mDepthFormat(depthFormat),
	mColorBufferUsage(colorUsage), mDepthBufferUsage(depthUsage),
	mSubpassDependencies(dependencies), mLoadOp(loadOp) {
		
	CreateRenderPass();

	mClearValues.resize(mColorFormats.size() + 1);
	for (uint32_t i = 0; i < mColorFormats.size(); i++)
		mClearValues[i] = { .0f, .0f, .0f, 0.f };
	mClearValues[mColorFormats.size()] = { 1.f, 0.f };

	mColorBuffers.resize(mColorFormats.size());
	
	vector<VkImageView> views(mColorBuffers.size() + 1);
	for (uint32_t i = 0; i < mColorBuffers.size(); i++) {
		mColorBuffers[i] = new Texture(mName + "ColorBuffer" + to_string(i), mDevice, mExtent, mColorFormats[i], 1, mSampleCount, mColorBufferUsage);
		views[i] = mColorBuffers[i]->View();
	}
	mDepthBuffer = new Texture(mName + "DepthBuffer", mDevice, mExtent, mDepthFormat, 1, mSampleCount, mDepthBufferUsage);
	views[views.size() - 1] = mDepthBuffer->View();
	
	VkFramebufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.attachmentCount = (uint32_t)views.size();
	info.pAttachments = views.data();
	info.renderPass = *mRenderPass;
	info.width = mExtent.width;
	info.height = mExtent.height;
	info.layers = 1;
	vkCreateFramebuffer(*mDevice, &info, nullptr, &mFramebuffer);
	mDevice->SetObjectName(mFramebuffer, mName, VK_OBJECT_TYPE_FRAMEBUFFER);

}
Framebuffer::~Framebuffer() {
	safe_delete(mRenderPass);
	for (uint32_t i = 0; i < mColorBuffers.size(); i++)
		safe_delete(mColorBuffers[i]);
	safe_delete(mDepthBuffer);
	vkDestroyFramebuffer(*mDevice, mFramebuffer, nullptr);
}

void Framebuffer::CreateRenderPass() {
	PROFILER_BEGIN("Create RenderPass");
	vector<VkAttachmentDescription> attachments(mColorFormats.size() + 1);
	for (uint32_t i = 0; i < mColorFormats.size(); i++) {
		attachments[i].format = mColorFormats[i];
		attachments[i].samples = mSampleCount;
		attachments[i].loadOp = mLoadOp;
		attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	attachments[mColorFormats.size()].format = mDepthFormat;
	attachments[mColorFormats.size()].samples = mSampleCount;
	attachments[mColorFormats.size()].loadOp = mLoadOp;
	attachments[mColorFormats.size()].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[mColorFormats.size()].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[mColorFormats.size()].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[mColorFormats.size()].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[mColorFormats.size()].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	safe_delete(mRenderPass);
	mRenderPass = new ::RenderPass(mName + "RenderPass", mDevice, attachments);
}

void Framebuffer::Clear(CommandBuffer* commandBuffer, ClearFlags clearFlags) {
	if (clearFlags == CLEAR_NONE) return;
	vector<VkClearAttachment> clears;

	if (clearFlags & CLEAR_COLOR) {
		clears.resize(mClearValues.size() - 1);
		for (uint32_t i = 0; i < mClearValues.size() - 1; i++) {
			clears[i].clearValue = mClearValues[i];
			clears[i].colorAttachment = i;
			clears[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
	}
	if (clearFlags & CLEAR_DEPTH) {
			VkClearAttachment c = {};
			c.clearValue = mClearValues[mColorFormats.size()];
			c.colorAttachment = mColorFormats.size();
			c.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			clears.push_back(c);
	}

	VkClearRect rect = {};
	rect.baseArrayLayer = 0;
	rect.layerCount = 1;
	rect.rect.extent = mExtent;
	vkCmdClearAttachments(*commandBuffer, clears.size(), clears.data(), 1, &rect);
}

void Framebuffer::ResolveColor(CommandBuffer* commandBuffer, uint32_t index, VkImage destination) {
	commandBuffer->TransitionBarrier(mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	if (mSampleCount == VK_SAMPLE_COUNT_1_BIT) {
		VkImageCopy region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(*commandBuffer,
			*mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	} else {
		VkImageResolve region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdResolveImage(*commandBuffer,
			*mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
}
void Framebuffer::ResolveDepth(CommandBuffer* commandBuffer, VkImage destination) {
	commandBuffer->TransitionBarrier(mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	if (mSampleCount == VK_SAMPLE_COUNT_1_BIT) {
		VkImageCopy region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vkCmdCopyImage(*commandBuffer,
			*mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	} else {
		VkImageResolve region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vkCmdResolveImage(*commandBuffer,
			*mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
}