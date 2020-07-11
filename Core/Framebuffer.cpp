#include <Core/Framebuffer.hpp>
#include <Core/CommandBuffer.hpp>
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

	mClearValues.resize(mColorFormats.size() + 1);
	for (uint32_t i = 0; i < mColorFormats.size(); i++)
		mClearValues[i] = { .0f, .0f, .0f, 0.f };
	mClearValues[mColorFormats.size()] = { 1.f, 0.f };

	mFramebuffers.resize(mDevice->Instance()->Window()->BackBufferCount());
	for (uint32_t i = 0; i < mFramebuffers.size(); i++) {
		mFramebuffers[i].mColorBuffers.resize(mColorFormats.size());
		memset(mFramebuffers[i].mColorBuffers.data(), 0, sizeof(Texture*) * mColorFormats.size());
	}

	CreateRenderPass();
	Create();
}
Framebuffer::~Framebuffer() {
	safe_delete(mRenderPass);
	for (uint32_t i = 0; i < mFramebuffers.size(); i++) {
		for (uint32_t j = 0; j < mFramebuffers[i].mColorBuffers.size(); j++)
			safe_delete(mFramebuffers[i].mColorBuffers[j]);
		safe_delete(mFramebuffers[i].mDepthBuffer);
		if (mFramebuffers[i].mFramebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(*mDevice, mFramebuffers[i].mFramebuffer, nullptr);
	}
}

void Framebuffer::Create() {		
	FramebufferData& fbd = mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()];

	PROFILER_BEGIN("Create Framebuffer");
	if (fbd.mFramebuffer != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(*mDevice);
		vkDestroyFramebuffer(*mDevice, fbd.mFramebuffer, nullptr);
	}

	vector<VkImageView> views(fbd.mColorBuffers.size() + 1);

	for (uint32_t i = 0; i < fbd.mColorBuffers.size(); i++) {
		safe_delete(fbd.mColorBuffers[i]);
		fbd.mColorBuffers[i] = new Texture(mName + "ColorBuffer" + to_string(i), mDevice, mExtent, mColorFormats[i], 1, mSampleCount, mColorBufferUsage);
		views[i] = fbd.mColorBuffers[i]->View();
	}

	safe_delete(fbd.mDepthBuffer);
	fbd.mDepthBuffer = new Texture(mName + "DepthBuffer", mDevice, mExtent, mDepthFormat, 1, mSampleCount, mDepthBufferUsage);
	views[views.size() - 1] = fbd.mDepthBuffer->View();
	
	VkFramebufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.attachmentCount = (uint32_t)views.size();
	info.pAttachments = views.data();
	info.renderPass = *mRenderPass;
	info.width = mExtent.width;
	info.height = mExtent.height;
	info.layers = 1;
	vkCreateFramebuffer(*mDevice, &info, nullptr, &fbd.mFramebuffer);
	mDevice->SetObjectName(fbd.mFramebuffer, mName, VK_OBJECT_TYPE_FRAMEBUFFER);
	PROFILER_END;
}

void Framebuffer::CreateRenderPass() {
	PROFILER_BEGIN("Create RenderPass");
	vector<VkAttachmentReference> colorAttachments(mColorFormats.size());
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

		colorAttachments[i].attachment = i;
		colorAttachments[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	attachments[mColorFormats.size()].format = mDepthFormat;
	attachments[mColorFormats.size()].samples = mSampleCount;
	attachments[mColorFormats.size()].loadOp = mLoadOp;
	attachments[mColorFormats.size()].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[mColorFormats.size()].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[mColorFormats.size()].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[mColorFormats.size()].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[mColorFormats.size()].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = (uint32_t)mColorFormats.size();
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	vector<VkSubpassDescription> subpasses(1);
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = (uint32_t)mColorFormats.size();
	subpasses[0].pColorAttachments = colorAttachments.data();
	subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

	safe_delete(mRenderPass);
	mRenderPass = new ::RenderPass(mName + "RenderPass", this, attachments, subpasses, mSubpassDependencies);
	PROFILER_END;
}

void Framebuffer::PreBeginRenderPass() {
	FramebufferData& fbd = mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()];
	if (fbd.mFramebuffer == VK_NULL_HANDLE || 
		fbd.mDepthBuffer->SampleCount() != mSampleCount || fbd.mDepthBuffer->Usage() != mDepthBufferUsage || (fbd.mColorBuffers.size() && fbd.mColorBuffers[0]->Usage() != mColorBufferUsage) || 
		fbd.mDepthBuffer->Extent().width != mExtent.width || fbd.mDepthBuffer->Extent().height != mExtent.height)
		Create();
}

void Framebuffer::BeginRenderPass(CommandBuffer* commandBuffer) {
	FramebufferData& fbd = mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()];
	for (uint32_t i = 0; i < fbd.mColorBuffers.size(); i++)
		commandBuffer->TransitionBarrier(fbd.mColorBuffers[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	commandBuffer->TransitionBarrier(fbd.mDepthBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer->BeginRenderPass(mRenderPass, { { 0, 0 }, mExtent }, fbd.mFramebuffer, mClearValues.data(), (uint32_t)mClearValues.size());
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
	Framebuffer::FramebufferData& fbd = mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()];
	commandBuffer->TransitionBarrier(fbd.mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	if (mSampleCount == VK_SAMPLE_COUNT_1_BIT) {
		VkImageCopy region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(*commandBuffer,
			*fbd.mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	} else {
		VkImageResolve region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdResolveImage(*commandBuffer,
			*fbd.mColorBuffers[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
}
void Framebuffer::ResolveDepth(CommandBuffer* commandBuffer, VkImage destination) {
	FramebufferData& fbd = mFramebuffers[mDevice->Instance()->Window()->BackBufferIndex()];
	commandBuffer->TransitionBarrier(fbd.mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	if (mSampleCount == VK_SAMPLE_COUNT_1_BIT) {
		VkImageCopy region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vkCmdCopyImage(*commandBuffer,
			*fbd.mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	} else {
		VkImageResolve region = {};
		region.extent = { mExtent.width, mExtent.height, 1 };
		region.dstSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		region.srcSubresource.layerCount = 1;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vkCmdResolveImage(*commandBuffer,
			*fbd.mDepthBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
}