#pragma once

#include "RenderPass.hpp"
#include "Texture.hpp"

namespace stm {

class Framebuffer : public DeviceResource {
private:
	vk::Framebuffer mFramebuffer;
	vk::Extent2D mExtent = { 0, 0 };
	stm::RenderPass& mRenderPass;
	vector<TextureView> mAttachments;

public:
	inline Framebuffer(const string& name, stm::RenderPass& renderPass, const vk::ArrayProxy<const TextureView>& attachments) : DeviceResource(renderPass.mDevice, name), mRenderPass(renderPass) {
		mAttachments.resize(renderPass.AttachmentDescriptions().size());
		vector<vk::ImageView> views(mAttachments.size());
		for (const auto& view : attachments) {
			size_t idx = renderPass.AttachmentMap().at(view.texture().Name());
			mAttachments[idx] = view;
			views[idx] = *view;
			mExtent = vk::Extent2D(max(mExtent.width , view.texture().Extent().width), max(mExtent.height, view.texture().Extent().height) );
		}
		mFramebuffer = renderPass.mDevice->createFramebuffer(vk::FramebufferCreateInfo({}, *mRenderPass, views, mExtent.width, mExtent.height, 1));
		renderPass.mDevice.SetObjectName(mFramebuffer, Name());
	}
	inline ~Framebuffer() { mDevice->destroyFramebuffer(mFramebuffer); }
	
	inline const vk::Framebuffer& operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline vk::Extent2D Extent() const { return mExtent; }
	inline const vector<TextureView>& Attachments() const { return mAttachments; }
	inline stm::RenderPass& RenderPass() const { return mRenderPass; };

	inline TextureView at(const RenderAttachmentId& id) const {
		return mRenderPass.AttachmentMap().count(id) ? mAttachments[mRenderPass.AttachmentMap().at(id)] : TextureView();
	}
};

}