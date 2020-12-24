#pragma once

#include "Device.hpp"

namespace stm {

class Framebuffer {
private:
	vk::Framebuffer mFramebuffer;
	vk::Extent2D mExtent;
	shared_ptr<stm::RenderPass> mRenderPass;
	map<RenderTargetIdentifier, shared_ptr<Texture>> mAttachments;
	bool mDeleteAttachments;
	string mName;

public:

	STRATUM_API Framebuffer(const string& name, shared_ptr<stm::RenderPass> renderPass, const vector<shared_ptr<Texture>>& attachments);
	STRATUM_API ~Framebuffer();
	
	inline vk::Framebuffer operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline const string& Name() const { return mName; }

	inline vk::Extent2D Extent() const { return mExtent; }
	inline shared_ptr<Texture> Attachment(const RenderTargetIdentifier& id) const { return mAttachments.at(id); }
	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }

	inline shared_ptr<stm::RenderPass> RenderPass() const { return mRenderPass; };
};


}