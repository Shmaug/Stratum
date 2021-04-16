#pragma once

#include "RenderPass.hpp"
#include "Texture.hpp"

namespace stm {

class Framebuffer : public DeviceResource {
private:
	vk::Framebuffer mFramebuffer;
	vk::Extent2D mExtent = { 0, 0 };
	stm::RenderPass& mRenderPass;
	vector<Texture::View> mAttachments;

public:
	inline Framebuffer(const string& name, stm::RenderPass& renderPass, const vk::ArrayProxy<const Texture::View>& attachments) : DeviceResource(renderPass.mDevice, name), mRenderPass(renderPass) {
		mAttachments.resize(renderPass.AttachmentDescriptions().size());
		vector<vk::ImageView> views(mAttachments.size());
		for (const auto& view : attachments) {
			size_t idx = renderPass.AttachmentIndex(view.texture().Name());
			mAttachments[idx] = view;
			views[idx] = *view;
			mExtent = vk::Extent2D(max(mExtent.width , view.texture().Extent().width), max(mExtent.height, view.texture().Extent().height) );
		}
		mFramebuffer = renderPass.mDevice->createFramebuffer(vk::FramebufferCreateInfo({}, *mRenderPass, views, mExtent.width, mExtent.height, 1));
		renderPass.mDevice.SetObjectName(mFramebuffer, Name());
	}
	template<convertible_to<Texture::View>... Args>
	inline Framebuffer(const string& name, stm::RenderPass& renderPass, Args&&... attachments) : Framebuffer(name, renderPass, { forward<Args>(attachments)... }) {}

	inline ~Framebuffer() { mDevice->destroyFramebuffer(mFramebuffer); }
	
	inline const vk::Framebuffer& operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline vk::Extent2D Extent() const { return mExtent; }
	inline stm::RenderPass& RenderPass() const { return mRenderPass; };

	inline size_t size() const { return mAttachments.size(); }
	inline auto begin() const { return mAttachments.begin(); }
	inline auto end() const { return mAttachments.end(); }

	inline const Texture::View& operator[](size_t index) const { return mAttachments[index]; }
	inline const Texture::View& operator[](const RenderAttachmentId& id) const { return mAttachments[mRenderPass.AttachmentIndex(id)]; }
};

}