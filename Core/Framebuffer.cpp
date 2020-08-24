#include <Core/Framebuffer.hpp>
#include <Core/RenderPass.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Window.hpp>
#include <Data/Texture.hpp>
#include <Util/Profiler.hpp>

using namespace std;

Framebuffer::Framebuffer(const string& name, ::RenderPass* renderPass, const vector<Texture*>& attachments) : mName(name), mRenderPass(renderPass), mDeleteAttachments(false) {
	mExtent = { 0, 0 };
	vector<vk::ImageView> views(attachments.size());
	for (uint32_t i = 0; i < attachments.size(); i++) {
		views[i] = attachments[i]->View();
		mAttachments.emplace(renderPass->AttachmentName(i), attachments[i]);
		mExtent = { max(mExtent.width, attachments[i]->Extent().width), max(mExtent.height, attachments[i]->Extent().height) };
	}
	
	vk::FramebufferCreateInfo info = {};
	info.attachmentCount = (uint32_t)views.size();
	info.pAttachments = views.data();
	info.renderPass = *mRenderPass;
	info.width = mExtent.width;
	info.height = mExtent.height;
	info.layers = 1;
	mFramebuffer = ((vk::Device)*renderPass->Device()).createFramebuffer(info);
	renderPass->Device()->SetObjectName(mFramebuffer, mName);
}

Framebuffer::~Framebuffer() {
	if (mDeleteAttachments) for (auto& kp : mAttachments) safe_delete(kp.second);
	mRenderPass->Device()->Destroy(mFramebuffer);
}