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
	mDevice.set_debug_name(mCommandBuffer, name);
	
	clear();
	mCommandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	mState = CommandBufferState::eRecording;
}

void CommandBuffer::begin_label(const string& text, const Vector4f& color) {
	if (vkCmdBeginDebugUtilsLabelEXT) {
		vk::DebugUtilsLabelEXT label = {};
		memcpy(label.color, &color, sizeof(color));
		label.pLabelName = text.c_str();
		vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, reinterpret_cast<VkDebugUtilsLabelEXT*>(&label));
	}
}
void CommandBuffer::end_label() {
	if (vkCmdEndDebugUtilsLabelEXT)
		vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
}

void CommandBuffer::clear() {
	mHeldResources.clear();
	mPrimitiveCount = 0;
	mBoundFramebuffer.reset();
	mSubpassIndex = 0;
	mBoundPipeline.reset();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
}
void CommandBuffer::reset(const string& name) {
	clear();
	
	mCommandBuffer.reset({});
	mDevice->resetFences({ **mCompletionFence });
	mDevice.set_debug_name(mCommandBuffer, name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;
}

void CommandBuffer::begin_render_pass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> framebuffer, const vk::ArrayProxy<const vk::ClearValue>& clearValues, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < framebuffer->size(); i++)
		(*framebuffer)[i].texture().transition_barrier(*this, get<vk::AttachmentDescription>(renderPass->attachments()[i]).initialLayout);

	vector<vk::ClearValue> vals(framebuffer->size());
	if (clearValues.empty()) {
		for (uint32_t i = 0; i < framebuffer->size(); i++)
			if (is_depth_stencil(framebuffer->at(i).texture().format()))
				vals[i] = vk::ClearValue( vk::ClearDepthStencilValue(1.f, 0) );
			else
				vals[i] = vk::ClearValue( vk::ClearColorValue(std::array<uint32_t,4>{0,0,0,0}) );
	} else
		ranges::copy(clearValues, vals.begin());

	mCommandBuffer.beginRenderPass(vk::RenderPassBeginInfo(**renderPass, **framebuffer, vk::Rect2D({}, framebuffer->extent()), vals), contents);

	mBoundFramebuffer = framebuffer;
	mSubpassIndex = 0;
	hold_resource(renderPass);
	hold_resource(framebuffer);
}
void CommandBuffer::next_subpass(vk::SubpassContents contents) {
	mCommandBuffer.nextSubpass(contents);
	mSubpassIndex++;
	for (uint32_t i = 0; i < mBoundFramebuffer->size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().subpasses()[mSubpassIndex].at(mBoundFramebuffer->render_pass().attachments()[i].second)).initialLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		attachment.texture().mTrackedLayout = layout;
		attachment.texture().mTrackedStageFlags = guess_stage(layout);
		attachment.texture().mTrackedAccessFlags = guess_access_flags(layout);
	}
}
void CommandBuffer::end_render_pass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mBoundFramebuffer->render_pass().attachments().size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().attachments()[i]).finalLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		attachment.texture().mTrackedLayout = layout;
		attachment.texture().mTrackedStageFlags = guess_stage(layout);
		attachment.texture().mTrackedAccessFlags = guess_access_flags(layout);
	}
	
	mBoundFramebuffer = nullptr;
	mSubpassIndex = -1;
}