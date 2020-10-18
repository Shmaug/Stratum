#pragma once

#include "Device.hpp"

namespace stm {

enum class AttachmentType {
	eUnused,
	eColor,
	eDepthStencil,
	eResolve,
	eInput,
	ePreserve,
};

struct SubpassAttachment {
	AttachmentType mType;
	vk::Format mFormat;
	vk::SampleCountFlagBits mSamples;
	vk::AttachmentLoadOp mLoadOp;
	vk::AttachmentStoreOp mStoreOp;
};

struct Subpass {
	ShaderPassIdentifier mShaderPass;
	std::map<RenderTargetIdentifier, SubpassAttachment> mAttachments;
	// Generally for dependencies between renderpasses or other situations where dependencies cant be resolved automatically
	std::map<RenderTargetIdentifier, std::pair<vk::PipelineStageFlags, vk::AccessFlags>> mAttachmentDependencies;
};

class RenderPass {
private:
	vk::RenderPass mRenderPass;

public:
	const std::string mName;
	stm::Device* const mDevice;

	STRATUM_API RenderPass(const std::string& name, stm::Device* device, const std::vector<Subpass>& subpasses);
	STRATUM_API ~RenderPass();

	inline vk::RenderPass operator*() const { return mRenderPass; }
	inline const vk::RenderPass* operator ->() const { return &mRenderPass; }

	inline const Subpass& GetSubpass(uint32_t index) const { return mSubpasses[index]; }
	inline uint32_t SubpassCount() const { return (uint32_t)mSubpasses.size(); }

	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }
	inline vk::AttachmentDescription Attachment(uint32_t index) const { return mAttachments[index]; }
	inline RenderTargetIdentifier AttachmentName(uint32_t index) const { return mAttachmentNames[index]; }
	inline uint32_t AttachmentIndex(const RenderTargetIdentifier& name) const { return mAttachmentMap.at(name); }
	inline vk::AttachmentDescription Attachment(const RenderTargetIdentifier& name) const { return mAttachments.at(mAttachmentMap.at(name)); }

private:
 	friend struct std::hash<RenderPass>;
 	friend class CommandBuffer;

	std::vector<vk::AttachmentDescription> mAttachments;
	std::vector<RenderTargetIdentifier> mAttachmentNames;
	std::map<RenderTargetIdentifier, uint32_t> mAttachmentMap;
	std::vector<Subpass> mSubpasses;
	uint64_t mSubpassHash;
};

}

namespace std {
template<>
struct hash<stm::RenderPass> {
	inline size_t operator()(const stm::RenderPass& rp) {
		return rp.mSubpassHash;
	}
};

template<>
struct hash<stm::SubpassAttachment> {
	inline size_t operator()(const stm::SubpassAttachment& a) {
		return stm::hash_combine(a.mType, a.mFormat, a.mSamples, a.mLoadOp, a.mStoreOp);
	}
};

template<>
struct hash<stm::Subpass> {
	inline size_t operator()(const stm::Subpass& p) {
		size_t h = stm::hash_combine(p.mShaderPass);
		for (const auto&[name, attachment] : p.mAttachments)
			h = stm::hash_combine(h, name, attachment);
		return h;
	}
};
};
