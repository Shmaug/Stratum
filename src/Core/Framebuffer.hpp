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
	inline Framebuffer(const string& name, stm::RenderPass& renderPass, const vector<TextureView>& attachments)
		: DeviceResource(renderPass.mDevice, name), mRenderPass(renderPass), mAttachments(attachments), mExtent({0, 0}) {

		vector<vk::ImageView> views(mAttachments.size());
		for (uint32_t i = 0; i < mAttachments.size(); i++) {
			views[i] = *mAttachments[i];
			mExtent = vk::Extent2D( max(mExtent.width, mAttachments[i].texture().Extent().width), max(mExtent.height, mAttachments[i].texture().Extent().height) );
		}
		
		vk::FramebufferCreateInfo info({}, *mRenderPass, views, mExtent.width, mExtent.height, 1);
		mFramebuffer = renderPass.mDevice->createFramebuffer(info);
		renderPass.mDevice.SetObjectName(mFramebuffer, mName);
	}
	inline ~Framebuffer() { mDevice->destroyFramebuffer(mFramebuffer); }
	
	inline vk::Framebuffer operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline const string& Name() const { return mName; }
	inline vk::Extent2D Extent() const { return mExtent; }
	inline const vector<TextureView>& Attachments() const { return mAttachments; }
	inline stm::RenderPass& RenderPass() const { return mRenderPass; };

	inline TextureView at(const RenderAttachmentId& id) const {
		return mRenderPass.AttachmentMap().count(id) ? mAttachments[mRenderPass.AttachmentMap().at(id)] : TextureView();
	}
};

}