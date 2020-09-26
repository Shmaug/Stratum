#pragma once

#include <Core/Device.hpp>

namespace stm {

class Framebuffer {
private:
	vk::Framebuffer mFramebuffer;
	vk::Extent2D mExtent;
	std::shared_ptr<stm::RenderPass> mRenderPass;
	std::map<RenderTargetIdentifier, std::shared_ptr<Texture>> mAttachments;
	bool mDeleteAttachments;

public:
	const std::string mName;

	STRATUM_API Framebuffer(const std::string& name, std::shared_ptr<stm::RenderPass> renderPass, const std::vector<std::shared_ptr<Texture>>& attachments);
	STRATUM_API ~Framebuffer();
	
	inline vk::Framebuffer operator*() const { return mFramebuffer; };
	inline const vk::Framebuffer* operator->() const { return &mFramebuffer; };
	
	inline vk::Extent2D Extent() const { return mExtent; }
	inline std::shared_ptr<Texture> Attachment(const RenderTargetIdentifier& id) const { return mAttachments.at(id); }
	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }

	inline std::shared_ptr<RenderPass> RenderPass() const { return mRenderPass; };
};


}