#pragma once

#include <Core/Device.hpp>

class Framebuffer {
public:
	const std::string mName;

	STRATUM_API Framebuffer(const std::string& name, ::RenderPass* renderPass, const std::vector<Texture*>& attachments);
	STRATUM_API ~Framebuffer();
	
	inline VkExtent2D Extent() const { return mExtent; }
	inline Texture* Attachment(const RenderTargetIdentifier& id) const { return mAttachments.at(id); }
	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }

	inline ::RenderPass* RenderPass() const { return mRenderPass; };
	inline operator VkFramebuffer() const { return mFramebuffer; };

private:
	VkFramebuffer mFramebuffer;
	VkExtent2D mExtent;
	::RenderPass* mRenderPass;
	std::unordered_map<RenderTargetIdentifier, Texture*> mAttachments;
	bool mDeleteAttachments;
};
