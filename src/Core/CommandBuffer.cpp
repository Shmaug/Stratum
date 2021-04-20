#include "CommandBuffer.hpp"

using namespace stm;

CommandBuffer::CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level)
	: DeviceResource(device, name), mQueueFamily(queueFamily) {
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdEndDebugUtilsLabelEXT");
	
	mCompletionFence = make_unique<Fence>(device, name + "_fence");
	mCommandPool = queueFamily->mCommandBuffers.at(this_thread::get_id()).first;

	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = level;
	allocInfo.commandBufferCount = 1;
	mCommandBuffer = mDevice->allocateCommandBuffers({ allocInfo })[0];
	mDevice.SetObjectName(mCommandBuffer, name);
	
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
	mBoundFramebuffer.reset();
	mSubpassIndex = 0;
	mBoundPipeline.reset();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
}
void CommandBuffer::Reset(const string& name) {
	Clear();
	
	mCommandBuffer.reset({});
	mDevice->resetFences({ **mCompletionFence });
	mDevice.SetObjectName(mCommandBuffer, name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;
}

void CommandBuffer::BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> framebuffer, const vector<vk::ClearValue>& clearValues, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->attachments().size(); i++)
		(*framebuffer)[i].texture().TransitionBarrier(*this, get<vk::AttachmentDescription>(renderPass->attachments()[i]).initialLayout);

	vk::RenderPassBeginInfo info = {};
	info.renderPass = **renderPass;
	info.framebuffer = **framebuffer;
	info.renderArea = vk::Rect2D({ 0, 0 }, framebuffer->extent());
	info.setClearValues(clearValues);
	mCommandBuffer.beginRenderPass(&info, contents);

	mBoundFramebuffer = framebuffer;
	mSubpassIndex = 0;
	HoldResource(renderPass);
	HoldResource(framebuffer);
}
void CommandBuffer::NextSubpass(vk::SubpassContents contents) {
	mCommandBuffer.nextSubpass(contents);
	mSubpassIndex++;
}
void CommandBuffer::EndRenderPass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mBoundFramebuffer->render_pass().attachments().size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().attachments()[i]).finalLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		attachment.texture().mTrackedLayout = layout;
		attachment.texture().mTrackedStageFlags = GuessStage(layout);
		attachment.texture().mTrackedAccessFlags = GuessAccessMask(layout);
	}
	
	mBoundFramebuffer = nullptr;
	mSubpassIndex = -1;
}