#include <Core/RenderPass.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Framebuffer.hpp>

using namespace std;

RenderPass::RenderPass(const string& name, ::Device* device, const vector<VkAttachmentDescription>& attachments)
	: mName(name), mDevice(device), mAttachments(attachments), mRenderPass(VK_NULL_HANDLE), mDirty(true) {
	vector<VkAttachmentReference> colorAttachments;
	VkAttachmentReference depthAttachmentRef = {};
	for (uint32_t i = 0; i < attachments.size(); i++) {
		if (attachments[i].initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			colorAttachments.push_back({i,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		else if (attachments[i].initialLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			depthAttachmentRef = { i, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	}

	VkSubpassDescription subpass;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = (uint32_t)colorAttachments.size();
	subpass.pColorAttachments = colorAttachments.data();
	subpass.pDepthStencilAttachment = &depthAttachmentRef; // TODO: pointer points to depthAttachmentRef on the stack
	AddSubpass(subpass);
}
RenderPass::~RenderPass() {
	if (mRenderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(*mDevice, mRenderPass, nullptr);
}

void RenderPass::Begin(CommandBuffer* commandBuffer, Framebuffer* framebuffer) {
	if (mDirty) {
		if (mRenderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(*mDevice, mRenderPass, nullptr);
		
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = (uint32_t)mAttachments.size();
		renderPassInfo.pAttachments = mAttachments.data();
		renderPassInfo.subpassCount = (uint32_t)mSubpasses.size();
		renderPassInfo.pSubpasses = mSubpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)mSubpassDependencies.size();
		renderPassInfo.pDependencies = mSubpassDependencies.data();
		ThrowIfFailed(vkCreateRenderPass(*mDevice, &renderPassInfo, nullptr, &mRenderPass), "vkCreateRenderPass failed");
		mDevice->SetObjectName(mRenderPass, mName + " RenderPass", VK_OBJECT_TYPE_RENDER_PASS);
	}
	
	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.renderPass = mRenderPass;
	info.clearValueCount = (uint32_t)framebuffer->ClearValues().size();
	info.pClearValues = framebuffer->ClearValues().data();
	info.renderArea = { { 0, 0 }, framebuffer->Extent() };
	info.framebuffer = *framebuffer;
	vkCmdBeginRenderPass(*commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
}