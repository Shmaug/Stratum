#pragma once

#include "Device.hpp"

namespace stm {

using RenderAttachmentId = string;

enum BlendMode {
	eOpaque,
	eAdd,
	eSubtract,
	eAlpha
};
inline vk::PipelineColorBlendAttachmentState blend_mode_state(BlendMode mode = BlendMode::eOpaque) {
	switch (mode) {
		default:
		case eOpaque:
			return vk::PipelineColorBlendAttachmentState(false,
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd, // color op
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd, // alpha op
					vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		case eAdd:
			return vk::PipelineColorBlendAttachmentState(true,
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd, // color op
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd, // alpha op
					vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		case eSubtract:
			return vk::PipelineColorBlendAttachmentState(true,
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eSubtract, // color op
					vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eSubtract, // alpha op
					vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
		case eAlpha:
			return vk::PipelineColorBlendAttachmentState(true,
					vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, // color op
					vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, // alpha op
					vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	}
}

enum AttachmentType {
	eInput = 1,
	eColor = 2,
	eResolve = 3,
	eDepthStencil = 4,
	ePreserve = 5
};

struct SubpassDescription {
public:
	struct AttachmentInfo {
		AttachmentType mType;
		vk::PipelineColorBlendAttachmentState mBlendState;
		vk::AttachmentDescription mDescription;
	};

	inline auto& attachments() { return mAttachmentDescriptions; }
	inline const auto& attachments() const { return mAttachmentDescriptions; }
	inline size_t count(const RenderAttachmentId& attachmentId) const { return mAttachmentDescriptions.count(attachmentId); }
	inline const auto& at(const RenderAttachmentId& attachmentId) const { return mAttachmentDescriptions.at(attachmentId); }
	inline auto& at(const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions.at(attachmentId); }
	template<typename... Args> requires(constructible_from<AttachmentInfo, Args...>)
	inline auto& emplace(const RenderAttachmentId& attachmentId, Args&&... args) { return mAttachmentDescriptions.emplace(attachmentId, AttachmentInfo(forward<Args>(args)...)).first->second; }
	inline size_t erase(const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions.erase(attachmentId); }
	
	inline AttachmentInfo& operator[](const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions[attachmentId]; }
	inline const AttachmentInfo& operator[](const RenderAttachmentId& attachmentId) const { return at(attachmentId); }

private:
	unordered_map<RenderAttachmentId, AttachmentInfo> mAttachmentDescriptions;
};

class RenderPass : public DeviceResource {
private:
 	friend class CommandBuffer;
	friend struct std::hash<stm::RenderPass>;

	vk::RenderPass mRenderPass;

	vector<SubpassDescription> mSubpassDescriptions;
	vector<pair<vk::AttachmentDescription, RenderAttachmentId>> mAttachmentDescriptions;
	unordered_map<RenderAttachmentId, uint32_t> mAttachmentMap;

	size_t mHash;

public:
	template<ranges::view R>
	inline RenderPass(Device& device, const string& name, R&& subpassDescriptions) : DeviceResource(device, name), mHash(0) {
		ranges::copy(subpassDescriptions, back_inserter(mSubpassDescriptions));
		
		vector<tuple<
			vector<vk::AttachmentReference>,
			vector<vk::AttachmentReference>,
			vector<vk::AttachmentReference>,
			vector<uint32_t>,
			vk::AttachmentReference >> subpassData(mSubpassDescriptions.size());
		vector<vk::SubpassDescription> subpasses(mSubpassDescriptions.size());
		vector<vk::SubpassDependency> dependencies;
		vector<vk::AttachmentDescription> attachments;

		for (uint32_t i = 0; i < mSubpassDescriptions.size(); i++) {
			auto&[inputAttachments, colorAttachments, resolveAttachments, preserveAttachments, depthAttachment] = subpassData[i];

			mHash = hash_combine(mHash, hash_args(mSubpassDescriptions[i].attachments()));

			bool hasDepth = false;
			for (const auto& [attachmentName,attachment] : mSubpassDescriptions[i].attachments()) {
				uint32_t attachmentIndex;
				const auto& vkdesc = attachment.mDescription;
				if (auto it = mAttachmentMap.find(attachmentName); it != mAttachmentMap.end()) {
					attachmentIndex = it->second;
					// follow final state of attachment through whole renderpass
					vk::AttachmentDescription& desc = mAttachmentDescriptions.at(attachmentIndex).first;
					desc.finalLayout = vkdesc.finalLayout;
					desc.storeOp = vkdesc.storeOp;
					desc.stencilStoreOp = vkdesc.stencilStoreOp;
				} else {
					attachmentIndex = (uint32_t)mAttachmentDescriptions.size();
					mAttachmentMap.emplace(attachmentName, attachmentIndex);
					mAttachmentDescriptions.emplace_back(make_pair(vkdesc, attachmentName));
					attachments.emplace_back(vkdesc);
				}
				switch (attachment.mType) {
					case AttachmentType::eColor:
						colorAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentType::eDepthStencil:
						hasDepth = true;
						depthAttachment = vk::AttachmentReference(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentType::eResolve:
						resolveAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentType::eInput:
						inputAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentType::ePreserve:
						preserveAttachments.emplace_back(attachmentIndex);
						break;
				}

				// detect and create dependencies, assuming subpasses are properly sorted
				unordered_map<uint32_t, vk::SubpassDependency> tmpdeps;
				for (int32_t srcSubpass = int32_t(i) - 1; srcSubpass >= 0; srcSubpass--) {
					if (mSubpassDescriptions[srcSubpass].count(attachmentName)) {
						const auto& srcAttachment = mSubpassDescriptions[srcSubpass].at(attachmentName);
						if (srcAttachment.mType == AttachmentType::eColor || srcAttachment.mType == AttachmentType::eDepthStencil || srcAttachment.mType == AttachmentType::eResolve) {
							vk::SubpassDependency* dep; 
							if (auto it = tmpdeps.find(srcSubpass); it == tmpdeps.end()) {
								dep = &tmpdeps.emplace(srcSubpass, vk::SubpassDependency(srcSubpass, i)).first->second;
								dep->dependencyFlags =  vk::DependencyFlagBits::eByRegion;
							} else
								dep = &tmpdeps[srcSubpass];
							dep->srcStageMask  |= guess_stage(srcAttachment.mDescription.initialLayout);
							dep->srcAccessMask |= guess_access_flags(srcAttachment.mDescription.initialLayout);
							dep->dstStageMask  |= guess_stage(attachment.mDescription.initialLayout);
							dep->dstAccessMask |= guess_access_flags(attachment.mDescription.initialLayout);
						}
					}
				}
				for (const auto&[src,dep] : tmpdeps)
					dependencies.emplace_back(dep);
			}

			subpasses[i] = vk::SubpassDescription(
				{}, vk::PipelineBindPoint::eGraphics,
				inputAttachments, colorAttachments, resolveAttachments, hasDepth ? &depthAttachment : nullptr, preserveAttachments);
		}

		mRenderPass = mDevice->createRenderPass(vk::RenderPassCreateInfo({}, attachments, subpasses, dependencies));
		mDevice.set_debug_name(mRenderPass, name);
	}
	inline ~RenderPass() {
		mDevice->destroyRenderPass(mRenderPass);
	}

	inline vk::RenderPass& operator*() { return mRenderPass; }
	inline vk::RenderPass* operator->() { return &mRenderPass; }
	inline const vk::RenderPass& operator*() const { return mRenderPass; }
	inline const vk::RenderPass* operator->() const { return &mRenderPass; }

	inline const auto& subpasses() const { return mSubpassDescriptions; }
	inline const auto& attachments() const { return mAttachmentDescriptions; }
	inline uint32_t attachment_index(const RenderAttachmentId& id) const { return mAttachmentMap.at(id); }
};

}

namespace std {

template<> struct hash<stm::RenderPass> {
	inline size_t operator()(const stm::RenderPass& rp) {
		return rp.mHash;
	}
};

}