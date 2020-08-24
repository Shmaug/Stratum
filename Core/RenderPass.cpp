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
				vk::AttachmentDescription2 d = {};
				d.format = kp.second.mFormat;
				d.samples = kp.second.mSamples;
				switch (kp.second.mType) {
					case ATTACHMENT_UNUSED: break;
					case ATTACHMENT_COLOR:
						d.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
						d.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
						break;
					case ATTACHMENT_DEPTH_STENCIL:
						d.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
						d.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
						break;
					case ATTACHMENT_RESOLVE:
						d.initialLayout = vk::ImageLayout::eGeneral;
						d.finalLayout = vk::ImageLayout::eGeneral;
						break;
					case ATTACHMENT_INPUT:
						d.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
						d.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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
			vector<vk::AttachmentReference2> inputAttachments;
			vector<vk::AttachmentReference2> colorAttachments;
			vector<vk::AttachmentReference2> resolveAttachments;
			vector<uint32_t> preserveAttachments;
			vk::AttachmentReference2 depthAttachment;
		};
		vector<SubpassData> subpassData(mSubpasses.size());
		vector<vk::SubpassDescription2> subpasses(mSubpasses.size());
		vector<vk::SubpassDependency2> dependencies;

		uint32_t index = 0;
		for (const auto& subpass : mSubpasses) {
			
			for (auto kp : subpass.mAttachments) {
				vk::SubpassDependency2 dep = {};
				dep.dstSubpass = index;
				dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;

				switch (kp.second.mType) {
					case ATTACHMENT_UNUSED: break;
					case ATTACHMENT_COLOR:
						dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						subpassData[index].colorAttachments.push_back(vk::AttachmentReference2(mAttachmentMap.at(kp.first), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor));
						break;
					case ATTACHMENT_DEPTH_STENCIL:
						dep.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
						subpassData[index].depthAttachment = vk::AttachmentReference2(mAttachmentMap.at(kp.first), vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth);
						break;
					case ATTACHMENT_RESOLVE:
						dep.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						subpassData[index].resolveAttachments.push_back(vk::AttachmentReference2(mAttachmentMap.at(kp.first), vk::ImageLayout::eColorAttachmentOptimal , vk::ImageAspectFlagBits::eColor));
						break;
					case ATTACHMENT_INPUT:
						dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
						dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
						subpassData[index].inputAttachments.push_back(vk::AttachmentReference2(mAttachmentMap.at(kp.first), vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor));
						break;
					case ATTACHMENT_PRESERVE:
						dep.dstAccessMask = {};
						dep.dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
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
							dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
							dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
							break;
						case ATTACHMENT_DEPTH_STENCIL:
							dep.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
							dep.srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
							break;
						case ATTACHMENT_RESOLVE:
							dep.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
							dep.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
							break;
						case ATTACHMENT_INPUT:
							dep.srcAccessMask = vk::AccessFlagBits::eShaderRead;
							break;
						case ATTACHMENT_PRESERVE:
							dep.srcAccessMask = {};
							dep.srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
							break;
						}
						dep.srcSubpass = srcIndex;
						dependencies.push_back(dep);
					}
					srcIndex++;
				}
			}

			subpasses[index].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpasses[index].inputAttachmentCount = (uint32_t)subpassData[index].inputAttachments.size();
			subpasses[index].pInputAttachments = subpassData[index].inputAttachments.data();
			subpasses[index].colorAttachmentCount = (uint32_t)subpassData[index].colorAttachments.size();
			subpasses[index].pColorAttachments = subpassData[index].colorAttachments.data();
			subpasses[index].pResolveAttachments = subpassData[index].resolveAttachments.data();
			subpasses[index].preserveAttachmentCount = (uint32_t)subpassData[index].preserveAttachments.size();
			subpasses[index].pPreserveAttachments = subpassData[index].preserveAttachments.data();
			subpasses[index].pDepthStencilAttachment = &subpassData[index].depthAttachment;
			
			index++;
		}

		vk::RenderPassCreateInfo2 renderPassInfo = {};
		renderPassInfo.attachmentCount = (uint32_t)mAttachments.size();
		renderPassInfo.pAttachments = mAttachments.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)dependencies.size();
		renderPassInfo.pDependencies = dependencies.data();
		mRenderPass = ((vk::Device)*mDevice).createRenderPass2(renderPassInfo);
		mDevice->SetObjectName(mRenderPass, mName + " RenderPass");
}
RenderPass::~RenderPass() {
	mDevice->Destroy(mRenderPass);
}