#include "CommandBuffer.hpp"
#include "Buffer.hpp"
#include "Framebuffer.hpp"
#include "RenderPass.hpp"

using namespace stm;

CommandBuffer::CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level)
	: mDevice(device), mName(name), mQueueFamily(queueFamily) {
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
	for (const auto& b : mBuffers) mDevice.PoolResource(b);
	for (const auto& t : mTextures) mDevice.PoolResource(t);
	for (const auto& ds : mDescriptorSets) mDevice.PoolResource(ds);
	mBuffers.clear();
	mTextures.clear();
	mDescriptorSets.clear();
	mRenderPasses.clear();
	mFramebuffers.clear();

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
bool CommandBuffer::CheckDone() {
	if (mState == CommandBufferState::eInFlight) {
		vk::Result status = mDevice->getFenceStatus(**mCompletionFence);
		if (status == vk::Result::eSuccess) {
			mState = CommandBufferState::eDone;
			Clear();
			return true;
		}
	}
	return mState == CommandBufferState::eDone;
}

void CommandBuffer::TrackResource(shared_ptr<Buffer> resource) { mBuffers.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<Texture> resource) { mTextures.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<DescriptorSet> resource) { mDescriptorSets.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<Framebuffer> resource) { mFramebuffers.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<RenderPass> resource) { mRenderPasses.push_back(resource); }

void CommandBuffer::BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> framebuffer, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	vector<vk::ClearValue> clearValues(renderPass->AttachmentDescriptions().size());
	for (uint32_t i = 0; i < renderPass->AttachmentDescriptions().size(); i++) {
		framebuffer->Attachments()[i]->TransitionBarrier(*this, get<vk::AttachmentDescription>(renderPass->AttachmentDescriptions()[i]).initialLayout);
		clearValues[i] = get<vk::ClearValue>(renderPass->AttachmentDescriptions()[i]);
	}

	vk::RenderPassBeginInfo info = {};
	info.renderPass = **renderPass;
	info.framebuffer = **framebuffer;
	info.renderArea = vk::Rect2D({ 0, 0 }, framebuffer->Extent());
	info.setClearValues(clearValues);
	mCommandBuffer.beginRenderPass(&info, contents);

	mCurrentRenderPass = renderPass;
	mCurrentFramebuffer = framebuffer;
	mCurrentSubpassIndex = 0;
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
		auto attachment = mCurrentFramebuffer->Attachments()[i];
		attachment->mTrackedLayout = layout;
		attachment->mTrackedStageFlags = GuessStage(layout);
		attachment->mTrackedAccessFlags = GuessAccessMask(layout);
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

void CommandBuffer::BindPipeline(shared_ptr<Pipeline> pipeline) {
	if (mBoundPipeline == pipeline) return;
	mCommandBuffer.bindPipeline(pipeline->BindPoint(), **pipeline);
	mBoundPipeline = pipeline;
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
}

bool CommandBuffer::PushConstant(const string& name, const byte_blob& data) {
	if (!mBoundPipeline) return false;
	auto it = mBoundPipeline->PushConstants().find(name);
	if (it == mBoundPipeline->PushConstants().end()) return false;
	vk::PushConstantRange range = it->second;
	mCommandBuffer.pushConstants(mBoundPipeline->Layout(), range.stageFlags, range.offset, min<uint32_t>((uint32_t)data.size(), range.size), data.data());
	return true;
}

void CommandBuffer::BindDescriptorSet(shared_ptr<DescriptorSet> descriptorSet, uint32_t set) {
	if (!mBoundPipeline) throw logic_error("cannot bind a descriptor set without a bound pipeline\n");
	if (mBoundDescriptorSets.size() <= set) mBoundDescriptorSets.resize(set + 1);
	else if (mBoundDescriptorSets[set] == descriptorSet) return;
	
	descriptorSet->FlushWrites();
	if (!mCurrentRenderPass)
		for (auto&[idx, entry] : descriptorSet->mBoundDescriptors) {
			switch (entry.mType) {
				case vk::DescriptorType::eSampler:
				case vk::DescriptorType::eUniformTexelBuffer:
				case vk::DescriptorType::eStorageTexelBuffer:
				case vk::DescriptorType::eUniformBuffer:
				case vk::DescriptorType::eStorageBuffer:
				case vk::DescriptorType::eUniformBufferDynamic:
				case vk::DescriptorType::eStorageBufferDynamic:
				case vk::DescriptorType::eInlineUniformBlockEXT:
				case vk::DescriptorType::eAccelerationStructureKHR:
					break;

				case vk::DescriptorType::eCombinedImageSampler:
				case vk::DescriptorType::eInputAttachment:
				case vk::DescriptorType::eSampledImage:
				case vk::DescriptorType::eStorageImage:
					entry.mTextureView.get()->TransitionBarrier(*this, entry.mImageLayout);
					break;
			}
		}
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->BindPoint(), mBoundPipeline->Layout(), set, { *descriptorSet }, {});
	mBoundDescriptorSets[set] = descriptorSet;
}