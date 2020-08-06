#pragma once

#include <Core/Device.hpp>

struct SubpassAttachment {
	VkFormat format;
	VkSampleCountFlagBits samples;
	VkImageLayout initialLayout;
	VkImageLayout finalLayout;
	VkAttachmentLoadOp loadOp;
	VkAttachmentStoreOp storeOp;
	inline operator VkAttachmentDescription2() const {
			VkAttachmentDescription2 d = {};
			d.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
			d.format = format;
			d.samples = samples;
			d.loadOp = loadOp;
			d.storeOp = storeOp;
			d.stencilLoadOp = loadOp;
			d.stencilStoreOp = storeOp;
			d.initialLayout = initialLayout;
			d.finalLayout = finalLayout;
			return d;
		}
};

struct Subpass {
	ShaderPassIdentifier mShaderPass;
	std::unordered_map<RenderTargetIdentifier, SubpassAttachment> mInputAttachments;
	std::unordered_map<RenderTargetIdentifier, SubpassAttachment> mColorAttachments;
	std::unordered_map<RenderTargetIdentifier, SubpassAttachment> mResolveAttachments;
	std::vector<RenderTargetIdentifier> mPreserveAttachments;
	std::pair<RenderTargetIdentifier, SubpassAttachment> mDepthAttachment;
	std::pair<RenderTargetIdentifier, SubpassAttachment> mDepthResolveAttachment;
	// If these are found within the same RenderPass, SubpassDependencies are created. If not, then the scene will use them to determine the order of RenderPass execution
	std::unordered_map<RenderTargetIdentifier, VkSubpassDependency2> mAttachmentDependencies;
};

class RenderPass {
public:
	const std::string mName;

	STRATUM_API RenderPass(const std::string& name, ::Device* device, const std::vector<Subpass>& subpasses, const std::vector<VkSubpassDependency2>& dependencies);
	STRATUM_API ~RenderPass();

	inline const Subpass& GetSubpass(uint32_t index) const { return mSubpasses[index]; }
	inline uint32_t SubpassCount() const { return (uint32_t)mSubpasses.size(); }

	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }
	inline VkAttachmentDescription2 Attachment(uint32_t index) const { return mAttachments[index]; }
	inline RenderTargetIdentifier AttachmentName(uint32_t index) const { return mAttachmentNames[index]; }
	inline uint32_t AttachmentIndex(const RenderTargetIdentifier& name) const { return mAttachmentMap.at(name); }
	inline VkAttachmentDescription2 Attachment(const RenderTargetIdentifier& name) const { return mAttachments.at(mAttachmentMap.at(name)); }

	inline ::Device* Device() const { return mDevice; }
	inline operator VkRenderPass() const { return mRenderPass; }

private:
 	friend struct std::hash<RenderPass>;
 	friend class CommandBuffer;
	::Device* mDevice;
	VkRenderPass mRenderPass;

	std::vector<VkAttachmentDescription2> mAttachments;
	std::vector<RenderTargetIdentifier> mAttachmentNames;
	std::unordered_map<RenderTargetIdentifier, uint32_t> mAttachmentMap;
	std::vector<Subpass> mSubpasses;
	uint64_t mSubpassHash;
};

namespace std {
	template<>
	struct hash<RenderPass> {
		inline size_t operator()(const RenderPass& rp) {
			return rp.mSubpassHash;
		}
	};
	
	template<>
	struct hash<SubpassAttachment> {
		inline size_t operator()(const SubpassAttachment& a) {
			size_t h = 0;
			hash_combine(h, a.format);
			hash_combine(h, a.samples);
			hash_combine(h, a.initialLayout);
			hash_combine(h, a.finalLayout);
			hash_combine(h, a.loadOp);
			hash_combine(h, a.storeOp);
			return h;
		}
	};

	template<>
	struct hash<Subpass> {
		inline size_t operator()(const Subpass& p) {
			size_t h = 0;
			hash_combine(h, p.mShaderPass);
			for (const auto& kp : p.mInputAttachments) { hash_combine(h, kp.first); hash_combine(h, kp.second); }
			for (const auto& kp : p.mColorAttachments) { hash_combine(h, kp.first); hash_combine(h, kp.second); }
			for (const auto& kp : p.mResolveAttachments) { hash_combine(h, kp.first); hash_combine(h, kp.second); }
			for (const auto& a : p.mPreserveAttachments) { hash_combine(h, a); }
			hash_combine(h, p.mDepthAttachment.first);
			hash_combine(h, p.mDepthAttachment.second);
			return h;
		}
	};
};