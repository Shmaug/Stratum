#include <Core/RenderPass.hpp>
#include <Data/Texture.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/Framebuffer.hpp>

using namespace std;
using namespace stm;

RenderPass::RenderPass(const string& name, stm::Device* device, const vector<Subpass>& subpassArray) : mName(name), mDevice(device), mSubpasses(subpassArray) {
		mSubpassHash = 0;
		for (uint32_t i = 0; i < subpassArray.size(); i++)
			mSubpassHash = hash_combine(mSubpassHash, subpassArray[i]);
		
		// build mAttachments and mAttachmentMap

		for (const auto& subpass : mSubpasses) {
			for (auto[name, attachment] : subpass.mAttachments) {
				if (mAttachmentMap.count(name)) continue;
				vk::AttachmentDescription d = {};
				d.format = attachment.mFormat;
				d.samples = attachment.mSamples;
				switch (attachment.mType) {
					case AttachmentType::eUnused: break;
					case AttachmentType::eColor:
						d.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
						d.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
						break;
					case AttachmentType::eDepthStencil:
						d.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
						d.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
						break;
					case AttachmentType::eResolve:
						d.initialLayout = vk::ImageLayout::eGeneral;
						d.finalLayout = vk::ImageLayout::eGeneral;
						break;
					case AttachmentType::eInput:
						d.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
						d.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
						break;
				}
				d.loadOp = attachment.mLoadOp;
				d.storeOp = attachment.mStoreOp;
				d.stencilLoadOp = attachment.mLoadOp;
				d.stencilStoreOp = attachment.mStoreOp;
				mAttachmentMap.emplace(name, (uint32_t)mAttachments.size());
				mAttachments.push_back(d);
				mAttachmentNames.push_back(name);
			}
		}
		
		struct SubpassData {
			vector<vk::AttachmentReference> inputAttachments;
			vector<vk::AttachmentReference> colorAttachments;
			vector<vk::AttachmentReference> resolveAttachments;
			vector<uint32_t> preserveAttachments;
			vk::AttachmentReference depthAttachment;
		};
		vector<SubpassData> subpassData(mSubpasses.size());
		vector<vk::SubpassDescription> subpasses(mSubpasses.size());
		vector<vk::SubpassDependency> dependencies;

		uint32_t index = 0;
		for (const auto& subpass : mSubpasses) {
			
			for (auto[name,attachment] : subpass.mAttachments) {
				vk::SubpassDependency dep = {};
				dep.dstSubpass = index;
				dep.dependencyFlags = vk::DependencyFlagBits::eByRegion;

				switch (attachment.mType) {
					case AttachmentType::eUnused: break;
					case AttachmentType::eColor:
						dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						subpassData[index].colorAttachments.push_back(vk::AttachmentReference(mAttachmentMap.at(name), vk::ImageLayout::eColorAttachmentOptimal));
						break;
					case AttachmentType::eDepthStencil:
						dep.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
						subpassData[index].depthAttachment = vk::AttachmentReference(mAttachmentMap.at(name), vk::ImageLayout::eDepthStencilAttachmentOptimal);
						break;
					case AttachmentType::eResolve:
						dep.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
						dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						subpassData[index].resolveAttachments.push_back(vk::AttachmentReference(mAttachmentMap.at(name), vk::ImageLayout::eColorAttachmentOptimal ));
						break;
					case AttachmentType::eInput:
						dep.dstAccessMask = vk::AccessFlagBits::eShaderRead;
						dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
						subpassData[index].inputAttachments.push_back(vk::AttachmentReference(mAttachmentMap.at(name), vk::ImageLayout::eShaderReadOnlyOptimal));
						break;
					case AttachmentType::ePreserve:
						dep.dstAccessMask = {};
						dep.dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
						subpassData[index].preserveAttachments.push_back(mAttachmentMap.at(name));
						break;
				}

				// Detect subpass dependencies from attachments

				uint32_t srcIndex = 0;
				for (const auto& srcSubpass : mSubpasses) {
					if (srcIndex >= index) break;
					for (const auto[srcName, srcAttachment] : srcSubpass.mAttachments) {
						if (srcName != name || srcAttachment.mType == AttachmentType::eUnused) continue;
						switch (srcAttachment.mType) {
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

		vk::RenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.attachmentCount = (uint32_t)mAttachments.size();
		renderPassInfo.pAttachments = mAttachments.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();
		renderPassInfo.dependencyCount = (uint32_t)dependencies.size();
		renderPassInfo.pDependencies = dependencies.data();
		mRenderPass = (*mDevice)->createRenderPass(renderPassInfo);
		mDevice->SetObjectName(mRenderPass, mName + " RenderPass");
}
RenderPass::~RenderPass() {
	(*mDevice)->destroyRenderPass(mRenderPass);
}