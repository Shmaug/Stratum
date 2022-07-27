#include "CommandBuffer.hpp"

using namespace stm;

CommandBuffer::CommandBuffer(Device::QueueFamily& queueFamily, const string& name, vk::CommandBufferLevel level)
	: DeviceResource(queueFamily.mDevice, name), mQueueFamily(queueFamily) {
	mFence = make_shared<Fence>(mDevice, name + "/fence");
	mCommandPool = mQueueFamily.mCommandBuffers.at(this_thread::get_id()).first;

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
CommandBuffer::~CommandBuffer() {
	if (mState == CommandBufferState::eInFlight)
		fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Destroying CommandBuffer [%s] that is in-flight!\n", name().c_str());
	clear();
	mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
}

void CommandBuffer::clear() {
	for (const auto& resource : mHeldResources)
		resource->mTracking.erase(this);
	mHeldResources.clear();
	mBoundFramebuffer.reset();
	mSubpassIndex = 0;
	mBoundPipeline.reset();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
	mSignalSemaphores.clear();
	mWaitSemaphores.clear();
}
void CommandBuffer::reset(const string& name) {
	clear();

	mCommandBuffer.reset({});

	mFence = make_shared<Fence>(mDevice, name + "/fence");
	mDevice.set_debug_name(mCommandBuffer, name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;
}

void CommandBuffer::bind_descriptor_set(uint32_t index, const shared_ptr<DescriptorSet>& descriptorSet, const vk::ArrayProxy<const uint32_t>& dynamicOffsets) {
	if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
	hold_resource(descriptorSet);

	descriptorSet->flush_writes();

	if (index >= mBoundDescriptorSets.size()) mBoundDescriptorSets.resize(index + 1);
	mBoundDescriptorSets[index] = descriptorSet;
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->bind_point(), mBoundPipeline->layout(), index, **descriptorSet, dynamicOffsets);
}

void CommandBuffer::begin_render_pass(const shared_ptr<RenderPass>& renderPass, const shared_ptr<Framebuffer>& framebuffer, const vk::Rect2D& renderArea, const vector<vk::ClearValue>& clearValues, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < framebuffer->size(); i++)
		(*framebuffer)[i].image()->transition_barrier(*this, get<vk::AttachmentDescription>(renderPass->attachments()[i]).initialLayout);

	mCommandBuffer.beginRenderPass(vk::RenderPassBeginInfo(**renderPass, **framebuffer, renderArea, clearValues), contents);

	mBoundFramebuffer = framebuffer;
	mSubpassIndex = 0;
	hold_resource(renderPass);
	hold_resource(framebuffer);
}
void CommandBuffer::next_subpass(vk::SubpassContents contents) {
	mCommandBuffer.nextSubpass(contents);
	mSubpassIndex++;
	for (uint32_t i = 0; i < mBoundFramebuffer->size(); i++) {
		const string& attachmentName = mBoundFramebuffer->render_pass().attachments()[i].second;
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().subpasses()[mSubpassIndex].at(attachmentName)).initialLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		uint32_t aspectMask = (uint32_t)attachment.subresource_range().aspectMask;
		while (aspectMask) {
			uint32_t aspect = 1 << countr_zero(aspectMask);
			aspectMask &= ~aspect;
			for (uint32_t layer = attachment.subresource_range().baseArrayLayer; layer < attachment.subresource_range().baseArrayLayer+attachment.subresource_range().layerCount; layer++)
				for (uint32_t level = attachment.subresource_range().baseMipLevel; level < attachment.subresource_range().baseMipLevel+attachment.subresource_range().levelCount; level++)
					attachment.image()->tracked_state((vk::ImageAspectFlags)aspect, layer, level) = make_tuple(layout, guess_stage(layout), guess_access_flags(layout));
		}
	}
}
void CommandBuffer::end_render_pass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mBoundFramebuffer->render_pass().attachments().size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().attachments()[i]).finalLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		uint32_t aspectMask = (uint32_t)attachment.subresource_range().aspectMask;
		while (aspectMask) {
			uint32_t aspect = 1 << countr_zero(aspectMask);
			aspectMask &= ~aspect;
			for (uint32_t layer = attachment.subresource_range().baseArrayLayer; layer < attachment.subresource_range().baseArrayLayer+attachment.subresource_range().layerCount; layer++)
				for (uint32_t level = attachment.subresource_range().baseMipLevel; level < attachment.subresource_range().baseMipLevel+attachment.subresource_range().levelCount; level++)
					attachment.image()->tracked_state((vk::ImageAspectFlags)aspect, layer, level) = make_tuple(layout, guess_stage(layout), guess_access_flags(layout));
		}
	}

	mBoundFramebuffer = nullptr;
	mSubpassIndex = -1;
}