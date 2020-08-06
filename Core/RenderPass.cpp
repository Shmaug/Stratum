#include <Core/RenderPass.hpp>
#include <Data/Texture.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Framebuffer.hpp>

using namespace std;

RenderPass::RenderPass(const string& name, ::Device* device, const vector<Subpass>& subpassArray, const vector<VkSubpassDependency2>& dependencies)
	: mName(name), mDevice(device), mSubpasses(subpassArray) {
		mSubpassHash = 0;
		for (uint32_t i = 0; i < subpassArray.size(); i++)
			hash_combine(mSubpassHash, subpassArray[i]);
			
		struct SubpassData {
			vector<VkAttachmentReference2> inputAttachments;
			vector<VkAttachmentReference2> colorAttachments;
			vector<VkAttachmentReference2> resolveAttachments;
			vector<uint32_t> preserveAttachments;
			VkAttachmentReference2 depthAttachment;
			VkAttachmentReference2 depthResolveAttachment;
		};
		vector<SubpassData> subpassData;

		vector<VkSubpassDescription2> subpasses;
		
		// build attachments list
		for (const auto& s : mSubpasses) {
			for (auto kp : s.mInputAttachments) {
				if (mAttachmentMap.count(kp.first)) continue;
				mAttachmentMap.emplace(kp.first, (uint32_t)mAttachments.size());
				mAttachmentNames.push_back(kp.first);
				mAttachments.push_back(kp.second);
			}
			for (auto kp : s.mColorAttachments) {
				if (mAttachmentMap.count(kp.first)) continue;
				mAttachmentMap.emplace(kp.first, (uint32_t)mAttachments.size());
				mAttachmentNames.push_back(kp.first);
				mAttachments.push_back(kp.second);
			}
			for (auto kp : s.mResolveAttachments) {
				if (mAttachmentMap.count(kp.first)) continue;
				mAttachmentMap.emplace(kp.first, (uint32_t)mAttachments.size());
				mAttachmentNames.push_back(kp.first);
				mAttachments.push_back(kp.second);
			}
			if (!s.mDepthAttachment.first.empty()) {
				if (!mAttachmentMap.count(s.mDepthAttachment.first)) {
					mAttachmentMap.emplace(s.mDepthAttachment.first, (uint32_t)mAttachments.size());
					mAttachmentNames.push_back(s.mDepthAttachment.first);
					mAttachments.push_back(s.mDepthAttachment.second);
				}
			}
			if (!s.mDepthResolveAttachment.first.empty()) {
				if (!mAttachmentMap.count(s.mDepthResolveAttachment.first)) {
					mAttachmentMap.emplace(s.mDepthResolveAttachment.first, (uint32_t)mAttachments.size());
					mAttachmentNames.push_back(s.mDepthResolveAttachment.first);
					mAttachments.push_back(s.mDepthResolveAttachment.second);
				}
			}

			subpassData.push_back({});
		}
		
		// build subpass list
		uint32_t index = 0;
		for (const auto& subpass : mSubpasses) {
			for (auto kp : subpass.mInputAttachments) subpassData[index].inputAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), kp.second.initialLayout, VK_IMAGE_ASPECT_COLOR_BIT });
			for (auto kp : subpass.mColorAttachments) subpassData[index].colorAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), kp.second.initialLayout, VK_IMAGE_ASPECT_COLOR_BIT });
			for (auto kp : subpass.mResolveAttachments) subpassData[index].resolveAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), kp.second.initialLayout , VK_IMAGE_ASPECT_COLOR_BIT});
			for (auto a : subpass.mPreserveAttachments) subpassData[index].preserveAttachments.push_back(mAttachmentMap.at(a));
			if (!subpass.mDepthAttachment.first.empty()) subpassData[index].depthAttachment = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(subpass.mDepthAttachment.first), subpass.mDepthAttachment.second.initialLayout, VK_IMAGE_ASPECT_DEPTH_BIT };
			if (!subpass.mDepthResolveAttachment.first.empty()) {
				subpassData[index].depthResolveAttachment = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(subpass.mDepthResolveAttachment.first), subpass.mDepthResolveAttachment.second.initialLayout, VK_IMAGE_ASPECT_DEPTH_BIT };
				VkSubpassDescriptionDepthStencilResolve depthResolve = {};
				depthResolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
				depthResolve.pDepthStencilResolveAttachment = &subpassData[index].depthResolveAttachment;
				depthResolve.depthResolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				depthResolve.stencilResolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			}

			subpasses.push_back({});
			VkSubpassDescription2& desc = subpasses.back();
			desc.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
			//desc.pNext = &depthResolve;
			desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			desc.inputAttachmentCount = (uint32_t)subpassData[index].inputAttachments.size();
			desc.pInputAttachments = subpassData[index].inputAttachments.data();
			desc.colorAttachmentCount = (uint32_t)subpassData[index].colorAttachments.size();
			desc.pColorAttachments = subpassData[index].colorAttachments.data();
			desc.pResolveAttachments = subpassData[index].resolveAttachments.data();
			desc.preserveAttachmentCount = (uint32_t)subpassData[index].preserveAttachments.size();
			desc.pPreserveAttachments = subpassData[index].preserveAttachments.data();
			if (!subpass.mDepthAttachment.first.empty()) desc.pDepthStencilAttachment = &subpassData[index].depthAttachment;
			if (!subpass.mDepthResolveAttachment.first.empty()) desc.pDepthStencilAttachment = &subpassData[index].depthAttachment;
			index++;
		}

		VkRenderPassCreateInfo2 renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
		renderPassInfo.attachmentCount = (uint32_t)mAttachments.size();
		renderPassInfo.pAttachments = mAttachments.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)dependencies.size();
		renderPassInfo.pDependencies = dependencies.data();
		ThrowIfFailed(vkCreateRenderPass2(*mDevice, &renderPassInfo, nullptr, &mRenderPass), "vkCreateRenderPass failed");
		mDevice->SetObjectName(mRenderPass, mName + " RenderPass", VK_OBJECT_TYPE_RENDER_PASS);
}
RenderPass::~RenderPass() {
	vkDestroyRenderPass(*mDevice, mRenderPass, nullptr);
}