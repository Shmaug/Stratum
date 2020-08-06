#include <Core/Framebuffer.hpp>
#include <Core/RenderPass.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Window.hpp>
#include <Data/Texture.hpp>
#include <Util/Profiler.hpp>

using namespace std;

Framebuffer::Framebuffer(const string& name, ::RenderPass* renderPass, const vector<Texture*>& attachments) : mName(name), mRenderPass(renderPass), mDeleteAttachments(false) {
	mExtent = { 0, 0 };
	vector<VkImageView> views(attachments.size());
	for (uint32_t i = 0; i < attachments.size(); i++) {
		views[i] = attachments[i]->View();
		mAttachments.emplace(renderPass->AttachmentName(i), attachments[i]);
		mExtent = { max(mExtent.width, attachments[i]->Extent().width), max(mExtent.height, attachments[i]->Extent().height) };
	}
	
	VkFramebufferCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.attachmentCount = (uint32_t)views.size();
	info.pAttachments = views.data();
	info.renderPass = *mRenderPass;
	info.width = mExtent.width;
	info.height = mExtent.height;
	info.layers = 1;
	vkCreateFramebuffer(*renderPass->Device(), &info, nullptr, &mFramebuffer);
	renderPass->Device()->SetObjectName(mFramebuffer, mName, VK_OBJECT_TYPE_FRAMEBUFFER);
}

Framebuffer::~Framebuffer() {
	if (mDeleteAttachments) for (auto& kp : mAttachments) safe_delete(kp.second);
	vkDestroyFramebuffer(*mRenderPass->Device(), mFramebuffer, nullptr);
}