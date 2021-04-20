#pragma once

#include "Device.hpp"

namespace stm {

typedef string RenderAttachmentId;

class RenderPass : public DeviceResource {
public:
	enum AttachmentType {
		eInput,
		eColor,
		eResolve,
		eDepthStencil,
		ePreserve
	};
	struct SubpassDescription {
		string mName;
		vk::PipelineBindPoint mBindPoint;
		unordered_map<RenderAttachmentId, tuple<vk::AttachmentDescription, AttachmentType, vk::PipelineColorBlendAttachmentState>> mAttachmentDescriptions;
		// names of attachments that this subpass depends on, and their accessflags for this subpass
		//unordered_map<string, vk::AccessFlags> mSubpassDependencies;
	};

	inline RenderPass(Device& device, const string& name, const vector<SubpassDescription>& subpassDescriptions)
		: DeviceResource(device, name), mSubpassDescriptions(subpassDescriptions),  mHash(hash_combine(name, subpassDescriptions)) {
		
		struct SubpassData {
			vector<vk::AttachmentReference> mInputAttachments;
			vector<vk::AttachmentReference> mColorAttachments;
			vector<vk::AttachmentReference> mResolveAttachments;
			vector<uint32_t> mPreserveAttachments;
			vk::AttachmentReference mDepthAttachment;
		};
		vector<SubpassData> subpassData(subpassDescriptions.size());
		vector<vk::SubpassDescription> subpasses(subpassDescriptions.size());
		vector<vk::SubpassDependency> dependencies;
		vector<vk::AttachmentDescription> attachments;

		for (uint32_t i = 0; i < subpasses.size(); i++) {
			auto& sp = subpasses[i];
			auto& sd = subpassData[i];

			for (const auto& [attachmentName,attachmentData] : subpassDescriptions[i].mAttachmentDescriptions) {

				// TODO: attachment order is just whatever they appear in the unordered_map. needs to match framebuffer creation
				
				const auto& vkdesc = get<vk::AttachmentDescription>(attachmentData);
				if (mAttachmentMap.count(attachmentName)) {
					// track layout of attachment through whole renderpass
					auto&[desc,id] = mAttachmentDescriptions[mAttachmentMap.at(attachmentName)];
					desc.finalLayout = vkdesc.finalLayout;
					desc.storeOp = vkdesc.storeOp;
					desc.stencilStoreOp = vkdesc.stencilStoreOp;
				} else {
					mAttachmentMap.emplace(attachmentName, mAttachmentDescriptions.size());
					mAttachmentDescriptions.push_back(make_tuple(vkdesc, attachmentName));
					attachments.push_back(vkdesc);
				}

				switch (get<AttachmentType>(attachmentData)) {
					case AttachmentType::eColor:
						sd.mColorAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eColorAttachmentOptimal);
						break;
					case AttachmentType::eDepthStencil:
						sd.mDepthAttachment = vk::AttachmentReference((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eDepthStencilAttachmentOptimal);
						break;
					case AttachmentType::eResolve:
						sd.mResolveAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eColorAttachmentOptimal );
						break;
					case AttachmentType::eInput:
						sd.mInputAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eShaderReadOnlyOptimal);
						break;
					case AttachmentType::ePreserve:
						sd.mPreserveAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName));
						break;
				}

				uint32_t srcSubpass = i;
				for (int32_t j = i - 1; j >= 0; j--)
					if (mSubpassDescriptions[j].mAttachmentDescriptions.count(attachmentName)) {
						AttachmentType t = get<AttachmentType>(mSubpassDescriptions[j].mAttachmentDescriptions.at(attachmentName));
						if (t == AttachmentType::eColor || t == AttachmentType::eDepthStencil || t == AttachmentType::eResolve) {
							srcSubpass = j;
							break;
						}
					}
				
				if (srcSubpass < i) {
					const auto& srcAttachment = mSubpassDescriptions[srcSubpass].mAttachmentDescriptions.at(attachmentName);
					vk::SubpassDependency dep(srcSubpass, i);
					dep.dependencyFlags =  vk::DependencyFlagBits::eByRegion;
					switch (get<AttachmentType>(srcAttachment)) {
					case AttachmentType::eColor:
						dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("Color attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					case AttachmentType::eDepthStencil:
						dep.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("DepthStencil attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					case AttachmentType::eResolve:
						dep.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
						if (get<vk::AttachmentDescription>(srcAttachment).storeOp != vk::AttachmentStoreOp::eStore)
							throw invalid_argument("Resolve attachment " + attachmentName + " must use vk::AttachmentStoreOp::eStore");
						break;
					default:
						throw invalid_argument("Attachment " + attachmentName + " cannot act as a dependency source");
					}
					switch (get<AttachmentType>(attachmentData)) {
						case AttachmentType::eColor:
							dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
							break;
						case AttachmentType::eDepthStencil:
							dep.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
							break;
						case AttachmentType::eResolve:
							dep.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
							dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
							break;
						case AttachmentType::eInput:
							dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
							dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
							break;
						case AttachmentType::ePreserve:
							dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
							dep.dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
							break;
					}
					dependencies.push_back(dep);
				}
			}

			sp.pipelineBindPoint = mSubpassDescriptions[i].mBindPoint;
			sp.pDepthStencilAttachment = &sd.mDepthAttachment;
			sp.setColorAttachments(sd.mColorAttachments);
			sp.setInputAttachments(sd.mInputAttachments);
			sp.setPreserveAttachments(sd.mPreserveAttachments);
			sp.setResolveAttachments(sd.mResolveAttachments);
		}

		mRenderPass = mDevice->createRenderPass(vk::RenderPassCreateInfo({}, attachments, subpasses, dependencies));
		mDevice.SetObjectName(mRenderPass, name);
	}
	inline ~RenderPass() {
		mDevice->destroyRenderPass(mRenderPass);
	}

	inline const vk::RenderPass& operator*() const { return mRenderPass; }
	inline const vk::RenderPass* operator->() const { return &mRenderPass; }

	inline const auto& subpasses() const { return mSubpassDescriptions; }
	inline const auto& attachments() const { return mAttachmentDescriptions; }
	inline size_t find(const RenderAttachmentId& id) const { return mAttachmentMap.at(id); }

private:
 	friend class CommandBuffer;
	friend struct std::hash<stm::RenderPass>;

	vk::RenderPass mRenderPass;

	vector<SubpassDescription> mSubpassDescriptions;
	vector<tuple<vk::AttachmentDescription, RenderAttachmentId>> mAttachmentDescriptions;
	unordered_map<RenderAttachmentId, size_t> mAttachmentMap;

	size_t mHash;
};

}

namespace std {
template<> struct hash<stm::RenderPass> {
	inline size_t operator()(const stm::RenderPass& rp) { return rp.mHash; }
};
template<> struct hash<stm::RenderPass::SubpassDescription> {
	inline size_t operator()(const stm::RenderPass::SubpassDescription& p) { return stm::hash_combine(p.mName, p.mAttachmentDescriptions); }
};
}