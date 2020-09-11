#pragma once

#include <Core/Device.hpp>

class Framebuffer {
private:
	vk::Framebuffer mFramebuffer;

public:
	const std::string mName;

	STRATUM_API Framebuffer(const std::string& name, ::RenderPass* renderPass, const std::vector<stm_ptr<Texture>>& attachments);
	STRATUM_API ~Framebuffer();
	
	inline vk::Extent2D Extent() const { return mExtent; }
	inline stm_ptr<Texture> Attachment(const RenderTargetIdentifier& id) const { return mAttachments.at(id); }
	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }

	inline ::RenderPass* RenderPass() const { return mRenderPass; };
	inline operator vk::Framebuffer() const { return mFramebuffer; };

private:
	vk::Extent2D mExtent;
	::RenderPass* mRenderPass;
	std::unordered_map<RenderTargetIdentifier, stm_ptr<Texture>> mAttachments;
	bool mDeleteAttachments;
};
