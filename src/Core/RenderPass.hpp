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
		// subpasses that must execute before this one
		unordered_set<string> mSubpassDependencies;
	};

	template<ranges::range R> requires(convertible_to<ranges::range_reference_t<R>, SubpassDescription&>)
	inline RenderPass(Device& device, const string& name, const R& subpassDescriptions)
		: DeviceResource(device, name), mHash(hash_combine(name, subpassDescriptions)) {
		
		// Resolve dependencies & sort subpasses
		unordered_set<const SubpassDescription*> remaining;
		for (const SubpassDescription& s : subpassDescriptions) remaining.insert(&s);

		struct SubpassData {
			vector<vk::AttachmentReference> inputAttachments;
			vector<vk::AttachmentReference> colorAttachments;
			vector<vk::AttachmentReference> resolveAttachments;
			vector<uint32_t> preserveAttachments;
			vk::AttachmentReference depthAttachment;
		};
		vector<SubpassData> subpassData(remaining.size());
		vector<vk::SubpassDescription> subpasses(remaining.size());
		vector<vk::SubpassDependency> dependencies;
		vector<vk::AttachmentDescription> attachments;

		for (uint32_t i = 0; i < subpasses.size(); i++) {
			auto& sp = subpasses[i];
			auto& sd = subpassData[i];

			// find subpass with the fewest unsatisfied dependencies
			const SubpassDescription* r_min = nullptr;
			size_t tmp = -1;
			for (const SubpassDescription* s : remaining) {
				size_t c = ranges::count_if(s->mSubpassDependencies, [&](const string& sdep) {
					return ranges::find_if(remaining, [&](const SubpassDescription* s2) { return s2->mName == sdep; }) != remaining.end();
				});
				if (c < tmp) {
					tmp = c;
					r_min = s;
				}
			}

			for (const auto& [attachmentName,d] : r_min->mAttachmentDescriptions) {
				const auto& attachmentDesc = get<vk::AttachmentDescription>(d);
				if (mAttachmentMap.count(attachmentName)) {
					// track layout of attachment through whole renderpass
					auto&[desc,clear,id] = mAttachmentDescriptions[mAttachmentMap.at(attachmentName)];
					desc.finalLayout = attachmentDesc.finalLayout;
					desc.storeOp = attachmentDesc.storeOp;
					desc.stencilStoreOp = attachmentDesc.stencilStoreOp;
				} else {
					mAttachmentMap.emplace(attachmentName, mAttachmentDescriptions.size());
					mAttachmentDescriptions.push_back(make_tuple(attachmentDesc, vk::ClearValue(), attachmentName));
					attachments.push_back(attachmentDesc);
				}

				vk::SubpassDependency dep = {};
				dep.dstSubpass = i;
				dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;
				switch (get<AttachmentType>(d)) {
					case AttachmentType::eColor:
						sd.colorAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eColorAttachmentOptimal);
						dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						break;
					case AttachmentType::eDepthStencil:
						sd.depthAttachment = vk::AttachmentReference((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eDepthStencilAttachmentOptimal);
						dep.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
						break;
					case AttachmentType::eResolve:
						sd.resolveAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eColorAttachmentOptimal );
						dep.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						break;
					case AttachmentType::eInput:
						sd.inputAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName), vk::ImageLayout::eShaderReadOnlyOptimal);
						dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
						dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
						break;
					case AttachmentType::ePreserve:
						sd.preserveAttachments.emplace_back((uint32_t)mAttachmentMap.at(attachmentName));
						dep.dstAccessMask = {};
						dep.dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
						break;
				}

				auto srcSubpass = ranges::find_if(mSubpassDescriptions, [&](const auto& s) { return r_min->mSubpassDependencies.count(attachmentName); });
				dep.srcSubpass = (uint32_t)ranges::distance(mSubpassDescriptions.begin(), srcSubpass);
				
				if (dep.srcSubpass < dep.dstSubpass) {
					switch (get<AttachmentType>(srcSubpass->mAttachmentDescriptions.at(attachmentName))) {
					case AttachmentType::eColor:
						dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						break;
					case AttachmentType::eDepthStencil:
						dep.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
						break;
					case AttachmentType::eResolve:
						dep.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
						break;
					case AttachmentType::eInput:
						dep.srcAccessMask = vk::AccessFlagBits::eShaderRead;
						break;
					case AttachmentType::ePreserve:
						dep.srcAccessMask = {};
						dep.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
						break;
					}
					dependencies.push_back(dep);
				}
			}

			auto& subpass = mSubpassDescriptions.emplace_back(*r_min);
			remaining.erase(r_min);

			sp.pipelineBindPoint = subpass.mBindPoint;
			sp.pDepthStencilAttachment = &sd.depthAttachment;
			sp.setColorAttachments(sd.colorAttachments);
			sp.setInputAttachments(sd.inputAttachments);
			sp.setPreserveAttachments(sd.preserveAttachments);
			sp.setResolveAttachments(sd.resolveAttachments);
		}

		mRenderPass = mDevice->createRenderPass(vk::RenderPassCreateInfo({}, attachments, subpasses, dependencies));
		mDevice.SetObjectName(mRenderPass, mName);
	}
	inline ~RenderPass() {
		mDevice->destroyRenderPass(mRenderPass);
	}

	inline vk::RenderPass operator*() const { return mRenderPass; }
	inline const vk::RenderPass* operator->() const { return &mRenderPass; }

	inline const string& Name() const { return mName; }
	inline const auto& SubpassDescriptions() const { return mSubpassDescriptions; }
	inline const auto& AttachmentDescriptions() const { return mAttachmentDescriptions; }
	inline const auto& AttachmentMap() const { return mAttachmentMap; }

private:
 	friend class CommandBuffer;
	friend struct std::hash<stm::RenderPass>;

	vk::RenderPass mRenderPass;

	vector<SubpassDescription> mSubpassDescriptions;
	vector<tuple<vk::AttachmentDescription, vk::ClearValue, RenderAttachmentId>> mAttachmentDescriptions;
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