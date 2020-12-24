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
	map<RenderTargetIdentifier, SubpassAttachment> mAttachments;
	// Generally for dependencies between renderpasses or other situations where dependencies cant be resolved automatically
	map<RenderTargetIdentifier, pair<vk::PipelineStageFlags, vk::AccessFlags>> mAttachmentDependencies;

	inline friend size_t basic_hash(const Subpass& p) {
		size_t h = basic_hash(p.mShaderPass);
		for (const auto&[name, attachment] : p.mAttachments)
			h = basic_hash(h, name, attachment);
		return h;
	}
};

class RenderPass {
public:
	// TODO: consider using numbers here instead?
	typedef string RenderTargetIdentifier;
	typedef string ShaderPassIdentifier;

	STRATUM_API RenderPass(const string& name, stm::Device& device, const vector<Subpass>& subpasses);
	STRATUM_API ~RenderPass();

	inline vk::RenderPass operator*() const { return mRenderPass; }
	inline const vk::RenderPass* operator ->() const { return &mRenderPass; }

	inline const string& Name() const { return mName; }
	inline stm::Device& Device() const { return mDevice; }

	inline uint32_t SubpassCount() const { return (uint32_t)mSubpasses.size(); }
	inline const stm::Subpass& Subpass(uint32_t index) const { return mSubpasses[index]; }

	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }
	inline uint32_t AttachmentIndex(const RenderTargetIdentifier& name) const { return mAttachmentMap.at(name); }
	inline RenderTargetIdentifier AttachmentName(uint32_t index) const { return mAttachmentNames[index]; }
	inline vk::AttachmentDescription Attachment(const RenderTargetIdentifier& name) const { return mAttachments.at(mAttachmentMap.at(name)); }
	inline vk::AttachmentDescription Attachment(uint32_t index) const { return mAttachments[index]; }

private:
 	friend class CommandBuffer;

	vk::RenderPass mRenderPass;
	string mName;
	stm::Device& mDevice;

	vector<vk::AttachmentDescription> mAttachments;
	vector<RenderTargetIdentifier> mAttachmentNames;
	map<RenderTargetIdentifier, uint32_t> mAttachmentMap;
	vector<stm::Subpass> mSubpasses;
	size_t mHash;

	inline friend size_t basic_hash(const RenderPass& rp) { return rp.mHash; }
};

}