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
	inline Framebuffer(stm::RenderPass& renderPass, const string& name, const vk::ArrayProxy<Texture::View>& attachments)
		: DeviceResource(renderPass.mDevice, name), mRenderPass(renderPass), mAttachments(attachments.begin(), attachments.end()) {		
		for (uint32_t i = 0; i < mAttachments.size(); i++)
			mExtent = vk::Extent2D(max(mExtent.width , mAttachments[i].texture()->extent().width), max(mExtent.height, mAttachments[i].texture()->extent().height) );
		vector<vk::ImageView> views(mAttachments.size());
		ranges::transform(mAttachments, views.begin(), &Texture::View::operator*);
		mFramebuffer = renderPass.mDevice->createFramebuffer(vk::FramebufferCreateInfo({}, *mRenderPass, views, mExtent.width, mExtent.height, 1));
		renderPass.mDevice.set_debug_name(mFramebuffer, name);
	}
	inline ~Framebuffer() {
		mDevice->destroyFramebuffer(mFramebuffer);
	}
	
	inline vk::Framebuffer& operator*() { return mFramebuffer; };
	inline vk::Framebuffer* operator->() { return &mFramebuffer; };
	inline const vk::Framebuffer& operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline const vk::Extent2D& extent() const { return mExtent; }
	inline stm::RenderPass& render_pass() const { return mRenderPass; };

	inline size_t size() const { return mAttachments.size(); }
	inline auto begin() const { return mAttachments.begin(); }
	inline auto end() const { return mAttachments.end(); }

	inline const Texture::View& at(size_t index) const { return mAttachments.at(index); }
	inline const Texture::View& at(const RenderAttachmentId& id) const { return mAttachments.at(mRenderPass.attachment_index(id)); }
	inline const Texture::View& operator[](size_t index) const { return at(index); }
	inline const Texture::View& operator[](const RenderAttachmentId& id) const { return at(mRenderPass.attachment_index(id)); }
};

}