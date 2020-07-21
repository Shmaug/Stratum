#pragma once

#include <Core/Device.hpp>
#include <Util/Util.hpp>

class RenderPass {
public:
	const std::string mName;

	ENGINE_EXPORT RenderPass(const std::string& name, ::Device* device, const std::vector<VkAttachmentDescription>& attachments);
	ENGINE_EXPORT ~RenderPass();

	inline void AddSubpass(const VkSubpassDescription& subpass) { mSubpasses.push_back(subpass); mDirty = true; }

	inline ::Device* Device() const { return mDevice; }
	inline operator VkRenderPass() const { return mRenderPass; }

private:
 	friend class CommandBuffer;
	::Device* mDevice;
	VkRenderPass mRenderPass;

	std::vector<VkAttachmentDescription> mAttachments;
	std::vector<VkSubpassDescription> mSubpasses;
	std::vector<VkSubpassDependency> mSubpassDependencies;

	bool mDirty;
	ENGINE_EXPORT void Begin(CommandBuffer* commandBuffer, Framebuffer* framebuffer);
};