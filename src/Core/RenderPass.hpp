#pragma once

#include "Device.hpp"

namespace stm {

using RenderAttachmentId = string;

class RenderPass : public DeviceResource {
public:
	enum AttachmentTypeFlags {
		eInput = 1,
		eColor = 2,
		eResolve = 4,
		eDepthStencil = 8,
		ePreserve = 16
	};

	struct SubpassDescription {
	public:
		using AttachmentInfo = tuple<AttachmentTypeFlags, vk::PipelineColorBlendAttachmentState, vk::AttachmentDescription>;
	private:
		vk::PipelineBindPoint mBindPoint;
		unordered_map<RenderAttachmentId, AttachmentInfo> mAttachmentDescriptions;
	public:
		inline void bind_point(const vk::PipelineBindPoint& bindPoint) { mBindPoint = bindPoint; }
		inline const auto& bind_point() const { return mBindPoint; }
		inline const auto& attachments() const { return mAttachmentDescriptions; }
		inline size_t count(const RenderAttachmentId& attachmentId) const { return mAttachmentDescriptions.count(attachmentId); }
		inline const auto& at(const RenderAttachmentId& attachmentId) const { return mAttachmentDescriptions.at(attachmentId); }
		inline auto& at(const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions.at(attachmentId); }
		template<typename... Args>
		inline auto& emplace(const RenderAttachmentId& attachmentId, Args&&... args) { return mAttachmentDescriptions.emplace(attachmentId, forward<Args>(args)...).first->second; }
		inline size_t erase(const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions.erase(attachmentId); }
		
		inline auto& operator[](const RenderAttachmentId& attachmentId) { return mAttachmentDescriptions[attachmentId]; }
		inline const auto& operator[](const RenderAttachmentId& attachmentId) const { return at(attachmentId); }
	};

	template<ranges::random_access_range R>
	inline RenderPass(Device& device, const string& name, const R& subpassDescriptions)
		: DeviceResource(device, name), mSubpassDescriptions(subpassDescriptions) {

		mHash = 0;
		
		struct SubpassData {
			vector<vk::AttachmentReference> mInputAttachments;
			vector<vk::AttachmentReference> mColorAttachments;
			vector<vk::AttachmentReference> mResolveAttachments;
			vector<uint32_t> mPreserveAttachments;
			vk::AttachmentReference mDepthAttachment;
		};
		vector<SubpassData> subpassData(mSubpassDescriptions.size());
		vector<vk::SubpassDescription> subpasses(mSubpassDescriptions.size());
		vector<vk::SubpassDependency> dependencies;
		vector<vk::AttachmentDescription> attachments;

		for (uint32_t i = 0; i < subpasses.size(); i++) {
			auto& sd = subpassData[i];

			mHash = hash_combine(mHash, hash_args(subpassDescriptions[i].bind_point(), subpassDescriptions[i].attachments()));

			for (const auto& [attachmentName,attachmentData] : mSubpassDescriptions[i].attachments()) {
				uint32_t attachmentIndex;
				const auto& vkdesc = get<vk::AttachmentDescription>(attachmentData);
				if (auto it = mAttachmentMap.find(attachmentName); it != mAttachmentMap.end()) {
					attachmentIndex = it->second;
					// track layout of attachment through whole renderpass
					auto& desc = get<vk::AttachmentDescription>(mAttachmentDescriptions.at(attachmentIndex));
					desc.finalLayout = vkdesc.finalLayout;
					desc.storeOp = vkdesc.storeOp;
					desc.stencilStoreOp = vkdesc.stencilStoreOp;
				} else {
					attachmentIndex = (uint32_t)mAttachmentDescriptions.size();
					mAttachmentMap.emplace(attachmentName, attachmentIndex);
					mAttachmentDescriptions.emplace_back(make_pair(vkdesc, attachmentName));
					attachments.emplace_back(vkdesc);
				}
				switch (get<AttachmentTypeFlags>(attachmentData)) {
					case AttachmentTypeFlags::eColor:
						sd.mColorAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentTypeFlags::eDepthStencil:
						sd.mDepthAttachment = vk::AttachmentReference(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentTypeFlags::eResolve:
						sd.mResolveAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentTypeFlags::eInput:
						sd.mInputAttachments.emplace_back(attachmentIndex, vkdesc.initialLayout);
						break;
					case AttachmentTypeFlags::ePreserve:
						sd.mPreserveAttachments.emplace_back(attachmentIndex);
						break;
				}

				// detect dependencies (assume subpasses are properly sorted)

				uint32_t srcSubpass = i;
				for (int32_t j = i - 1; j >= 0; j--)
					if (mSubpassDescriptions[j].count(attachmentName)) {
						AttachmentTypeFlags t = get<AttachmentTypeFlags>(mSubpassDescriptions[j].at(attachmentName));
						if (t == AttachmentTypeFlags::eColor || t == AttachmentTypeFlags::eDepthStencil || t == AttachmentTypeFlags::eResolve) {
							srcSubpass = j;
							break;
						}
					}
				
				if (srcSubpass < i) {
					const auto& srcAttachment = mSubpassDescriptions[srcSubpass].at(attachmentName);
					vk::SubpassDependency dep(srcSubpass, i);
					dep.dependencyFlags =  vk::DependencyFlagBits::eByRegion;
					switch (get<AttachmentTypeFlags>(srcAttachment)) {
					case AttachmentTypeFlags::eColor:
						dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("Color attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					case AttachmentTypeFlags::eDepthStencil:
						dep.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("DepthStencil attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					case AttachmentTypeFlags::eResolve:
						dep.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("Resolve attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					default:
						throw invalid_argument("Attachment " + attachmentName + " cannot act as a dependency source");
					}
					switch (get<AttachmentTypeFlags>(attachmentData)) {
						case AttachmentTypeFlags::eColor:
							dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
							break;
						case AttachmentTypeFlags::eDepthStencil:
							dep.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
							break;
						case AttachmentTypeFlags::eResolve:
							dep.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
							break;
						case AttachmentTypeFlags::eInput:
							dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
							dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
							break;
						case AttachmentTypeFlags::ePreserve:
							dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
							dep.dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
							break;
					}
					dependencies.emplace_back(dep);
				}
			}

			auto& sp = subpasses[i];
			sp.pipelineBindPoint = mSubpassDescriptions[i].bind_point();
			sp.pDepthStencilAttachment = &sd.mDepthAttachment;
			sp.setColorAttachments(sd.mColorAttachments);
			sp.setInputAttachments(sd.mInputAttachments);
			sp.setPreserveAttachments(sd.mPreserveAttachments);
			sp.setResolveAttachments(sd.mResolveAttachments);
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

private:
 	friend class CommandBuffer;
	friend struct std::hash<stm::RenderPass>;

	vk::RenderPass mRenderPass;

	vector<SubpassDescription> mSubpassDescriptions;
	vector<pair<vk::AttachmentDescription, RenderAttachmentId>> mAttachmentDescriptions;
	unordered_map<RenderAttachmentId, uint32_t> mAttachmentMap;

	size_t mHash;
};

}

namespace std {

template<> struct hash<stm::RenderPass> {
	inline size_t operator()(const stm::RenderPass& rp) {
		return rp.mHash;
	}
};

}