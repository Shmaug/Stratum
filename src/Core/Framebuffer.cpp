#include <Core/Framebuffer.hpp>
#include <Core/RenderPass.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Window.hpp>
#include <Data/Texture.hpp>

using namespace std;
using namespace stm;

Framebuffer::Framebuffer(const string& name, shared_ptr<stm::RenderPass> renderPass, const vector<shared_ptr<Texture>>& attachments) : mName(name), mRenderPass(renderPass), mDeleteAttachments(false) {
	mExtent = { 0, 0 };
	vector<vk::ImageView> views(attachments.size());
	for (uint32_t i = 0; i < attachments.size(); i++) {
		views[i] = attachments[i]->View();
		mAttachments.emplace(renderPass->AttachmentName(i), attachments[i]);
		mExtent = { std::max(mExtent.width, attachments[i]->Extent().width), std::max(mExtent.height, attachments[i]->Extent().height) };
	}
	
	vk::FramebufferCreateInfo info({}, **mRenderPass, views, mExtent.width, mExtent.height, 1);
	info.attachmentCount = (uint32_t)views.size();
	info.pAttachments = views.data();
	info.renderPass = **mRenderPass;
	info.width = mExtent.width;
	info.height = mExtent.height;
	info.layers = 1;
	mFramebuffer = (*renderPass->mDevice)->createFramebuffer(info);
	renderPass->mDevice->SetObjectName(mFramebuffer, mName);
}

Framebuffer::~Framebuffer() {
	(*mRenderPass->mDevice)->destroyFramebuffer(mFramebuffer);
}