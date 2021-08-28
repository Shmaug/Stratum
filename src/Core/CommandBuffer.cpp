#include "CommandBuffer.hpp"

using namespace stm;

CommandBuffer::CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level)
	: DeviceResource(device, name), mQueueFamily(queueFamily) {
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)mDevice.mInstance->getProcAddr("vkCmdEndDebugUtilsLabelEXT");
	
	mCompletionFence = make_unique<Fence>(device, name + "/CompletionFence");
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
CommandBuffer::~CommandBuffer() {
	if (mState == CommandBufferState::eInFlight)
		fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Destroying CommandBuffer [%s] that is in-flight!\n", name().c_str());
	clear();
	mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
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
	for (const auto& resource : mHeldResources)
		resource->mTracking.erase(this); 
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


void CommandBuffer::bind_descriptor_set(uint32_t index, const shared_ptr<DescriptorSet>& descriptorSet, const vk::ArrayProxy<const uint32_t>& dynamicOffsets) {
	if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
	hold_resource(descriptorSet);
	
	descriptorSet->flush_writes();
	if (!mBoundFramebuffer) transition_images(*descriptorSet);

	if (index >= mBoundDescriptorSets.size()) mBoundDescriptorSets.resize(index + 1);
	mBoundDescriptorSets[index] = descriptorSet;
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->bind_point(), mBoundPipeline->layout(), index, **descriptorSet, dynamicOffsets);
}

void CommandBuffer::begin_render_pass(const shared_ptr<RenderPass>& renderPass, const shared_ptr<Framebuffer>& framebuffer, const vk::Rect2D& renderArea, const vk::ArrayProxyNoTemporaries<const vk::ClearValue>& clearValues, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < framebuffer->size(); i++)
		(*framebuffer)[i].texture()->transition_barrier(*this, get<vk::AttachmentDescription>(renderPass->attachments()[i]).initialLayout);

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
		auto layout = mBoundFramebuffer->render_pass().subpasses()[mSubpassIndex].at(attachmentName).mDescription.initialLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		attachment.texture()->mTrackedLayout = layout;
		attachment.texture()->mTrackedStageFlags = guess_stage(layout);
		attachment.texture()->mTrackedAccessFlags = guess_access_flags(layout);
	}
}
void CommandBuffer::end_render_pass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mBoundFramebuffer->render_pass().attachments().size(); i++) {
		auto layout = get<vk::AttachmentDescription>(mBoundFramebuffer->render_pass().attachments()[i]).finalLayout;
		auto& attachment = (*mBoundFramebuffer)[i];
		attachment.texture()->mTrackedLayout = layout;
		attachment.texture()->mTrackedStageFlags = guess_stage(layout);
		attachment.texture()->mTrackedAccessFlags = guess_access_flags(layout);
	}
	
	mBoundFramebuffer = nullptr;
	mSubpassIndex = -1;
}