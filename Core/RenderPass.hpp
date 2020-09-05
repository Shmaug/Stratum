#pragma once

#include <Core/Device.hpp>

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
	std::unordered_map<RenderTargetIdentifier, SubpassAttachment> mAttachments;
	// Generally for dependencies between renderpasses or other situations where dependencies cant be resolved automatically
	std::unordered_map<RenderTargetIdentifier, std::pair<vk::PipelineStageFlags, vk::AccessFlags>> mAttachmentDependencies;
};

class RenderPass {
private:
	vk::RenderPass mRenderPass;

public:
	const std::string mName;

	STRATUM_API RenderPass(const std::string& name, ::Device* device, const std::vector<Subpass>& subpasses);
	STRATUM_API ~RenderPass();

	inline const Subpass& GetSubpass(uint32_t index) const { return mSubpasses[index]; }
	inline uint32_t SubpassCount() const { return (uint32_t)mSubpasses.size(); }

	inline uint32_t AttachmentCount() const { return (uint32_t)mAttachments.size(); }
	inline vk::AttachmentDescription Attachment(uint32_t index) const { return mAttachments[index]; }
	inline RenderTargetIdentifier AttachmentName(uint32_t index) const { return mAttachmentNames[index]; }
	inline uint32_t AttachmentIndex(const RenderTargetIdentifier& name) const { return mAttachmentMap.at(name); }
	inline vk::AttachmentDescription Attachment(const RenderTargetIdentifier& name) const { return mAttachments.at(mAttachmentMap.at(name)); }

	inline ::Device* Device() const { return mDevice; }
	inline operator vk::RenderPass() const { return mRenderPass; }

private:
 	friend struct std::hash<RenderPass>;
 	friend class CommandBuffer;
	::Device* mDevice;

	std::vector<vk::AttachmentDescription> mAttachments;
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
			hash_combine(h, a.mType);
			hash_combine(h, a.mFormat);
			hash_combine(h, a.mSamples);
			hash_combine(h, a.mLoadOp);
			hash_combine(h, a.mStoreOp);
			return h;
		}
	};

	template<>
	struct hash<Subpass> {
		inline size_t operator()(const Subpass& p) {
			size_t h = 0;
			hash_combine(h, p.mShaderPass);
			for (const auto&[name,attachment] : p.mAttachments) { hash_combine(h, name); hash_combine(h, attachment); }
			return h;
		}
	};
};