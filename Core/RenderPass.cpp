#include <Core/RenderPass.hpp>
#include <Data/Texture.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Framebuffer.hpp>

using namespace std;

RenderPass::RenderPass(const string& name, ::Device* device, const vector<Subpass>& subpassArray) : mName(name), mDevice(device), mSubpasses(subpassArray) {
		mSubpassHash = 0;
		for (uint32_t i = 0; i < subpassArray.size(); i++)
			hash_combine(mSubpassHash, subpassArray[i]);
		
		// build mAttachments and mAttachmentMap

		for (const auto& subpass : mSubpasses) {
			for (auto kp : subpass.mAttachments) {
				if (mAttachmentMap.count(kp.first)) continue;
				VkAttachmentDescription2 d = {};
				d.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
				d.format = kp.second.mFormat;
				d.samples = kp.second.mSamples;
				switch (kp.second.mType) {
					case ATTACHMENT_UNUSED: break;
					case ATTACHMENT_COLOR:
						d.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						d.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						break;
					case ATTACHMENT_DEPTH_STENCIL:
						d.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
						d.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
						break;
					case ATTACHMENT_RESOLVE:
						d.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
						d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
						break;
					case ATTACHMENT_INPUT:
						d.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						d.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						break;
				}
				d.loadOp = kp.second.mLoadOp;
				d.storeOp = kp.second.mStoreOp;
				d.stencilLoadOp = kp.second.mLoadOp;
				d.stencilStoreOp = kp.second.mStoreOp;
				mAttachmentMap.emplace(kp.first, (uint32_t)mAttachments.size());
				mAttachments.push_back(d);
				mAttachmentNames.push_back(kp.first);
			}
		}
		
		struct SubpassData {
			vector<VkAttachmentReference2> inputAttachments;
			vector<VkAttachmentReference2> colorAttachments;
			vector<VkAttachmentReference2> resolveAttachments;
			vector<uint32_t> preserveAttachments;
			VkAttachmentReference2 depthAttachment;
		};
		vector<SubpassData> subpassData(mSubpasses.size());
		vector<VkSubpassDescription2> subpasses(mSubpasses.size());
		vector<VkSubpassDependency2> dependencies;

		uint32_t index = 0;
		for (const auto& subpass : mSubpasses) {
			subpasses[index].sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
			
			for (auto kp : subpass.mAttachments) {
				VkSubpassDependency2 dep = {};
				dep.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
				dep.dstSubpass = index;
				dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

				switch (kp.second.mType) {
					case ATTACHMENT_UNUSED: break;
					case ATTACHMENT_COLOR:
						dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
						dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
						subpassData[index].colorAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT });
						break;
					case ATTACHMENT_DEPTH_STENCIL:
						dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
						dep.dstStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
						subpassData[index].depthAttachment = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT };
						break;
					case ATTACHMENT_RESOLVE:
						dep.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
						subpassData[index].resolveAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL , VK_IMAGE_ASPECT_COLOR_BIT});
						break;
					case ATTACHMENT_INPUT:
						dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
						dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
						subpassData[index].inputAttachments.push_back({ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, mAttachmentMap.at(kp.first), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT });
						break;
					case ATTACHMENT_PRESERVE:
						dep.dstAccessMask = 0;
						dep.dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
						subpassData[index].preserveAttachments.push_back(mAttachmentMap.at(kp.first));
						break;
				}

				// Detect subpass dependencies from attachments

				uint32_t srcIndex = 0;
				for (const auto& srcSubpass : mSubpasses) {
					if (srcIndex >= index) break;
					for (const auto srcKp : srcSubpass.mAttachments) {
						if (srcKp.first != kp.first || srcKp.second.mType == ATTACHMENT_UNUSED) continue;
						switch (srcKp.second.mType) {
						case ATTACHMENT_COLOR:
							dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
							dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
							break;
						case ATTACHMENT_DEPTH_STENCIL:
							dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
							dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
							break;
						case ATTACHMENT_RESOLVE:
							dep.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
							dep.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
							break;
						case ATTACHMENT_INPUT:
							dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
							break;
						case ATTACHMENT_PRESERVE:
							dep.srcAccessMask = 0;
							dep.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
							break;
						}
						dep.srcSubpass = srcIndex;
						dependencies.push_back(dep);
					}
					srcIndex++;
				}
			}

			subpasses[index].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[index].inputAttachmentCount = (uint32_t)subpassData[index].inputAttachments.size();
			subpasses[index].pInputAttachments = subpassData[index].inputAttachments.data();
			subpasses[index].colorAttachmentCount = (uint32_t)subpassData[index].colorAttachments.size();
			subpasses[index].pColorAttachments = subpassData[index].colorAttachments.data();
			subpasses[index].pResolveAttachments = subpassData[index].resolveAttachments.data();
			subpasses[index].preserveAttachmentCount = (uint32_t)subpassData[index].preserveAttachments.size();
			subpasses[index].pPreserveAttachments = subpassData[index].preserveAttachments.data();
			if (subpassData[index].depthAttachment.sType == VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2)
				subpasses[index].pDepthStencilAttachment = &subpassData[index].depthAttachment;
			
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