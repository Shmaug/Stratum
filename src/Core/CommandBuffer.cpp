#include "CommandBuffer.hpp"
#include "Buffer.hpp"
#include "Framebuffer.hpp"
#include "RenderPass.hpp"

using namespace stm;

CommandBuffer::CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level)
	: DeviceResource(device, name), mQueueFamily(queueFamily) {
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdEndDebugUtilsLabelEXT");
	
	mCompletionFence = make_unique<Fence>(device, mName + "_fence");
	mCommandPool = queueFamily->mCommandBuffers.at(this_thread::get_id()).first;

	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = level;
	allocInfo.commandBufferCount = 1;
	mCommandBuffer = mDevice->allocateCommandBuffers({ allocInfo })[0];
	mDevice.SetObjectName(mCommandBuffer, mName);
	
	Clear();
	mCommandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	mState = CommandBufferState::eRecording;
}


void CommandBuffer::BeginLabel(const string& text, const Vector4f& color) {
	if (vkCmdBeginDebugUtilsLabelEXT) {
		vk::DebugUtilsLabelEXT label = {};
		memcpy(label.color, &color, sizeof(color));
		label.pLabelName = text.c_str();
		vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, reinterpret_cast<VkDebugUtilsLabelEXT*>(&label));
	}
}
void CommandBuffer::EndLabel() {
	if (vkCmdEndDebugUtilsLabelEXT)
		vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
}

void CommandBuffer::Clear() {
	mHeldResources.clear();
	mSignalSemaphores.clear();
	mWaitSemaphores.clear();
	mPrimitiveCount = 0;
	mCurrentFramebuffer.reset();
	mCurrentRenderPass.reset();
	mCurrentSubpassIndex = 0;
	mBoundPipeline.reset();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
}
void CommandBuffer::Reset(const string& name) {
	Clear();
	
	mCommandBuffer.reset({});
	mDevice->resetFences({ **mCompletionFence });
	mDevice.SetObjectName(mCommandBuffer, mName = name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;
}

void CommandBuffer::BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> framebuffer, const vector<vk::ClearValue>& clearValues, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->AttachmentDescriptions().size(); i++)
		framebuffer->Attachments()[i].texture().TransitionBarrier(*this, get<vk::AttachmentDescription>(renderPass->AttachmentDescriptions()[i]).initialLayout);

	vk::RenderPassBeginInfo info = {};
	info.renderPass = **renderPass;
	info.framebuffer = **framebuffer;
	info.renderArea = vk::Rect2D({ 0, 0 }, framebuffer->Extent());
	info.setClearValues(clearValues);
	mCommandBuffer.beginRenderPass(&info, contents);

	mCurrentRenderPass = renderPass;
	mCurrentFramebuffer = framebuffer;
	mCurrentSubpassIndex = 0;
	HoldResource(renderPass);
	HoldResource(framebuffer);
}
void CommandBuffer::NextSubpass(vk::SubpassContents contents) {
	mCommandBuffer.nextSubpass(contents);
	mCurrentSubpassIndex++;
}
void CommandBuffer::EndRenderPass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mCurrentRenderPass->AttachmentDescriptions().size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mCurrentRenderPass->AttachmentDescriptions()[i]).finalLayout;
		auto& attachment = mCurrentFramebuffer->Attachments()[i];
		attachment.texture().mTrackedLayout = layout;
		attachment.texture().mTrackedStageFlags = GuessStage(layout);
		attachment.texture().mTrackedAccessFlags = GuessAccessMask(layout);
	}
	
	mCurrentRenderPass = nullptr;
	mCurrentFramebuffer = nullptr;
	mCurrentSubpassIndex = -1;
}

void CommandBuffer::ClearAttachments(const vector<vk::ClearAttachment>& values) {
	vk::ClearRect rect = {};
	rect.layerCount = 1;
	rect.rect = vk::Rect2D { {}, mCurrentFramebuffer->Extent() };
	mCommandBuffer.clearAttachments((uint32_t)values.size(), values.data(), 1, &rect);
}

bool CommandBuffer::PushConstant(const string& name, const byte_blob& data) {
	if (!mBoundPipeline) return false;
	auto it = mBoundPipeline->PushConstants().find(name);
	if (it == mBoundPipeline->PushConstants().end()) return false;
	vk::PushConstantRange range = it->second;
	mCommandBuffer.pushConstants(mBoundPipeline->Layout(), range.stageFlags, range.offset, min<uint32_t>((uint32_t)data.size(), range.size), data.data());
	return true;
}

void CommandBuffer::BindPipeline(shared_ptr<Pipeline> pipeline) {
	if (mBoundPipeline == pipeline) return;
	mCommandBuffer.bindPipeline(pipeline->BindPoint(), **pipeline);
	mBoundPipeline = pipeline;
	mBoundDescriptorSets.clear();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	HoldResource(pipeline);
}

void CommandBuffer::BindDescriptorSets(const vector<shared_ptr<DescriptorSet>>& descriptorSets, uint32_t index) {
	if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
	if (mBoundDescriptorSets.size() < index + descriptorSets.size()) mBoundDescriptorSets.resize(index + descriptorSets.size());

	vector<vk::DescriptorSet> sets;
	sets.reserve(descriptorSets.size());
	for (const auto& descriptorSet : descriptorSets) {
		descriptorSet->FlushWrites();
		if (!mCurrentRenderPass)
			for (auto&[idx, entry] : descriptorSet->mBoundDescriptors) {
				switch (descriptorSet->layout_at(idx >> 32).mDescriptorType) {
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eInputAttachment:
					case vk::DescriptorType::eSampledImage:
					case vk::DescriptorType::eStorageImage: {
						const auto& t = get<tuple<shared_ptr<Sampler>, TextureView, vk::ImageLayout>>(entry);
						get<TextureView>(t).texture().TransitionBarrier(*this, get<vk::ImageLayout>(t));
						break;
					}
				}
			}
		mBoundDescriptorSets[index + sets.size()] = descriptorSet;
		sets.push_back(**descriptorSet);
		HoldResource(descriptorSet);
	}
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->BindPoint(), mBoundPipeline->Layout(), index, sets, {});
}
void CommandBuffer::BindDescriptorSet(shared_ptr<DescriptorSet> descriptorSet, uint32_t index) {
	if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
	if (mBoundDescriptorSets.size() <= index) mBoundDescriptorSets.resize(index + 1);
	else if (mBoundDescriptorSets[index] == descriptorSet) return;
	
	descriptorSet->FlushWrites();
	if (!mCurrentRenderPass)
		for (auto&[idx, entry] : descriptorSet->mBoundDescriptors) {
			switch (descriptorSet->layout_at(idx >> 32).mDescriptorType) {
				case vk::DescriptorType::eCombinedImageSampler:
				case vk::DescriptorType::eInputAttachment:
				case vk::DescriptorType::eSampledImage:
				case vk::DescriptorType::eStorageImage: {
					const auto& t = get<tuple<shared_ptr<Sampler>, TextureView, vk::ImageLayout>>(entry);
					get<TextureView>(t).texture().TransitionBarrier(*this, get<vk::ImageLayout>(t));
					break;
				}
			}
		}
	mBoundDescriptorSets[index] = descriptorSet;
	HoldResource(descriptorSet);
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->BindPoint(), mBoundPipeline->Layout(), index, { *descriptorSet }, {});
}