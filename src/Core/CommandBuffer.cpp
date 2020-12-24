#include "CommandBuffer.hpp"
#include "Buffer.hpp"
#include "Framebuffer.hpp"
#include "RenderPass.hpp"
#include "../Scene/Scene.hpp"


using namespace stm;

inline vk::AccessFlags GuessAccessMask(vk::ImageLayout layout) {
	switch (layout) {
    case vk::ImageLayout::eUndefined:
    case vk::ImageLayout::ePresentSrcKHR:
    case vk::ImageLayout::eColorAttachmentOptimal:
			return {};

    case vk::ImageLayout::eGeneral:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

    case vk::ImageLayout::eDepthAttachmentOptimal:
    case vk::ImageLayout::eStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
    case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    case vk::ImageLayout::eDepthReadOnlyOptimal:
    case vk::ImageLayout::eStencilReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead;
		
    case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead;
    case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
	}
	return vk::AccessFlagBits::eShaderRead;
}
inline vk::PipelineStageFlags GuessStage(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eGeneral:
			return vk::PipelineStageFlagBits::eComputeShader;

		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::PipelineStageFlagBits::eColorAttachmentOutput;
		
		case vk::ImageLayout::eShaderReadOnlyOptimal:
		case vk::ImageLayout::eDepthReadOnlyOptimal:
		case vk::ImageLayout::eStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::PipelineStageFlagBits::eFragmentShader;

		case vk::ImageLayout::eTransferSrcOptimal:
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::PipelineStageFlagBits::eTransfer;

		case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		case vk::ImageLayout::eStencilAttachmentOptimal:
		case vk::ImageLayout::eDepthAttachmentOptimal:
		case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
			return vk::PipelineStageFlagBits::eLateFragmentTests;

		case vk::ImageLayout::ePresentSrcKHR:
		case vk::ImageLayout::eSharedPresentKHR:
			return vk::PipelineStageFlagBits::eBottomOfPipe;

		default:
			return vk::PipelineStageFlagBits::eTopOfPipe;
	}
}

CommandBuffer::CommandBuffer(const string& name, stm::Device& device, stm::QueueFamily* queueFamily, vk::CommandBufferLevel level)
	: mName(name), mDevice(device), mQueueFamily(queueFamily), mSignalFence(new Fence(mName + "/SignalFence", device)) {
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)mDevice.Instance()->getProcAddr("vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)mDevice.Instance()->getProcAddr("vkCmdEndDebugUtilsLabelEXT");
	
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
CommandBuffer::~CommandBuffer() {
	if (mState == CommandBufferState::eInFlight)
		fprintf_color(ConsoleColorBits::eYellow, stderr, "destroying a CommandBuffer %s that is in-flight\n", mName.c_str());
	Clear();
	mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
	delete mSignalFence;
}

void CommandBuffer::BeginLabel(const string& text, const float4& color) {
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
	mTriangleCount = 0;

	mCurrentFramebuffer.reset();
	mCurrentRenderPass.reset();
	mCurrentSubpassIndex = 0;
	mCurrentShaderPass.clear();
	mBoundPipeline.reset();
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
	mBoundDescriptorSets.clear();
}
void CommandBuffer::Reset(const string& name) {
	Clear();
	
	mCommandBuffer.reset({});
	mDevice->resetFences({ **mSignalFence });
	mDevice.SetObjectName(mCommandBuffer, mName = name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;
}
bool CommandBuffer::CheckDone() {
	if (mState == CommandBufferState::eInFlight) {
		vk::Result status = mDevice->getFenceStatus(**mSignalFence);
		if (status == vk::Result::eSuccess) {
			mState = CommandBufferState::eDone;
			Clear();
			return true;
		}
	}
	return mState == CommandBufferState::eDone;
}

shared_ptr<Buffer> CommandBuffer::GetBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	auto b = mDevice.GetPooledBuffer(name, size, usage, memoryProperties);
	mBuffers.push_back(b);
	return b;
}
shared_ptr<Texture> CommandBuffer::GetTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits sampleCount, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	auto tex = mDevice.GetPooledTexture(name, extent, format, mipLevels, sampleCount, usage, memoryProperties);
	mTextures.push_back(tex);
	return tex;
}
shared_ptr<DescriptorSet> CommandBuffer::GetDescriptorSet(const string& name, vk::DescriptorSetLayout layout) {
	auto ds = mDevice.GetPooledDescriptorSet(name, layout);
	mDescriptorSets.push_back(ds);
	return ds;
}

void CommandBuffer::TrackResource(shared_ptr<Buffer> resource) { mBuffers.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<Texture> resource) { mTextures.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<DescriptorSet> resource) { mDescriptorSets.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<Framebuffer> resource) { mFramebuffers.push_back(resource); }
void CommandBuffer::TrackResource(shared_ptr<RenderPass> resource) { mRenderPasses.push_back(resource); }

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), { barrier }, {}, {});
}
void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, { barrier }, {});
}
void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, {}, { barrier });
}
void CommandBuffer::Barrier(Texture& texture) {
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = texture.mTrackedLayout;
	barrier.newLayout = texture.mTrackedLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *texture;
	barrier.srcAccessMask = texture.mTrackedAccessFlags;
	barrier.dstAccessMask = texture.mTrackedAccessFlags;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = texture.mArrayLayers;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = texture.mMipLevels;
	barrier.subresourceRange.aspectMask = texture.mAspectFlags;
	Barrier(texture.mTrackedStageFlags, texture.mTrackedStageFlags, barrier);
}
void CommandBuffer::TransitionBarrier(Texture& texture, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, texture.mTrackedStageFlags, GuessStage(newLayout), texture.mTrackedLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture& texture, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture& texture, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, texture.mTrackedStageFlags, dstStage, texture.mTrackedLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture& texture, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	if (newLayout == vk::ImageLayout::eUndefined) {
		texture.mTrackedLayout = newLayout;
		texture.mTrackedStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
		texture.mTrackedAccessFlags = {};
		return;
	}
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *texture;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = texture.MipLevels();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = texture.ArrayLayers();
	barrier.srcAccessMask = texture.mTrackedAccessFlags;
	barrier.dstAccessMask = GuessAccessMask(newLayout);
	barrier.subresourceRange.aspectMask = texture.mAspectFlags;
	Barrier(srcStage, dstStage, barrier);
	texture.mTrackedLayout = newLayout;
	texture.mTrackedStageFlags = dstStage;
	texture.mTrackedAccessFlags = barrier.dstAccessMask;
}
void CommandBuffer::TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	TransitionBarrier(image, subresourceRange, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = subresourceRange;
	barrier.srcAccessMask = GuessAccessMask(oldLayout);
	barrier.dstAccessMask = GuessAccessMask(newLayout);
	Barrier(srcStage, dstStage, barrier);
}

void CommandBuffer::BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> framebuffer, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->AttachmentCount(); i++)
		TransitionBarrier(*framebuffer->Attachment(renderPass->AttachmentName(i)), renderPass->Attachment(i).initialLayout);

	// Assign clear values specified by the render pass
	vector<vk::ClearValue> clearValues(renderPass->mAttachments.size());
	for (const auto& [name, attachment] : renderPass->mSubpasses[0].mAttachments)
		if (attachment.mType == AttachmentType::eDepthStencil)
			clearValues[renderPass->mAttachmentMap.at(name)].depthStencil = vk::ClearDepthStencilValue { 1.0f, 0 };
		else
			clearValues[renderPass->mAttachmentMap.at(name)].color.setFloat32({ 0.f, 0.f, 0.f, 0.f });

	vk::RenderPassBeginInfo info = {};
	info.renderPass = **renderPass;
	info.framebuffer = **framebuffer;
	info.renderArea = vk::Rect2D({ 0, 0 }, framebuffer->Extent());
	info.clearValueCount = (uint32_t)clearValues.size();
	info.pClearValues = clearValues.data();
	mCommandBuffer.beginRenderPass(&info, contents);

	mCurrentRenderPass = renderPass;
	mCurrentFramebuffer = framebuffer;
	mCurrentSubpassIndex = 0;
	mCurrentShaderPass = renderPass->mSubpasses[0].mShaderPass;
}
void CommandBuffer::NextSubpass(vk::SubpassContents contents) {
	mCommandBuffer.nextSubpass(contents);
	mCurrentSubpassIndex++;
	mCurrentShaderPass = mCurrentRenderPass->mSubpasses[mCurrentSubpassIndex].mShaderPass;
}
void CommandBuffer::EndRenderPass() {
	mCommandBuffer.endRenderPass();

	// Update tracked image layouts
	for (uint32_t i = 0; i < mCurrentRenderPass->AttachmentCount(); i++){
		auto attachment = mCurrentFramebuffer->Attachment(mCurrentRenderPass->AttachmentName(i));
		attachment->mTrackedLayout = mCurrentRenderPass->Attachment(i).finalLayout;
		attachment->mTrackedStageFlags = GuessStage(mCurrentRenderPass->Attachment(i).finalLayout);
		attachment->mTrackedAccessFlags = GuessAccessMask(mCurrentRenderPass->Attachment(i).finalLayout);
	}
	
	mCurrentRenderPass = nullptr;
	mCurrentFramebuffer = nullptr;
	mCurrentShaderPass = "";
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
	if (!mCurrentRenderPass) {
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
					TransitionBarrier(*entry.mImageView.mTexture, entry.mImageView.mLayout);
					break;
			}
		}
	}
	mCommandBuffer.bindDescriptorSets(mBoundPipeline->BindPoint(), mBoundPipeline->Layout(), set, { *descriptorSet }, {});
	mBoundDescriptorSets[set] = descriptorSet;
}

void CommandBuffer::BindVertexBuffer(const ArrayBufferView& view, uint32_t index) {
	if (mBoundVertexBuffers[index] == view) return;
	mCommandBuffer.bindVertexBuffers(index, { **view.mBuffer }, { view.mBufferOffset });
	mBoundVertexBuffers[index] = view;
}
void CommandBuffer::BindIndexBuffer(const ArrayBufferView& view) {
	if (mBoundIndexBuffer == view) return;
	mCommandBuffer.bindIndexBuffer(**view.mBuffer, view.mBufferOffset, view.mElementSize == sizeof(uint16_t) ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
	mBoundIndexBuffer = view;
}

void CommandBuffer::DrawMesh(Mesh& mesh, uint32_t instanceCount, uint32_t firstInstance) {
	auto pipeline = dynamic_pointer_cast<GraphicsPipeline>(mBoundPipeline);
	if (!mBoundPipeline) throw logic_error("cannot draw a mesh without a bound graphics pipeline\n");

	// TODO: this generates an entire vertex input struct each time it's called...
	auto vinput = mesh.CreateInput(pipeline->SpirvModules()[0]);
	for (uint32_t i = 0; i < vinput.mBindings.size(); i++)
		BindVertexBuffer(vinput.mBindings[i].mBufferView, i);
	if (mesh.IndexBuffer().mBuffer) BindIndexBuffer(mesh.IndexBuffer());

	uint32_t drawCount = 0;
	for (uint32_t i = 0; i < mesh.SubmeshCount(); i++) {
		auto& s = mesh.GetSubmesh(i);
		if (s.mIndexCount) {
			mCommandBuffer.drawIndexed(s.mIndexCount, instanceCount, s.mBaseIndex, s.mBaseVertex, firstInstance);
			drawCount += s.mIndexCount;
		} else {
			mCommandBuffer.draw(s.mVertexCount, instanceCount, s.mBaseVertex, firstInstance);
			drawCount += s.mVertexCount;
		}
	}
	if (mesh.Topology() == vk::PrimitiveTopology::eTriangleList)
		mTriangleCount += instanceCount * (drawCount / 3);
	else if (mesh.Topology() == vk::PrimitiveTopology::eTriangleStrip)
		mTriangleCount += instanceCount * (drawCount - 2);
	else if (mesh.Topology() == vk::PrimitiveTopology::eTriangleFan)
		mTriangleCount += instanceCount * (drawCount - 1);
}
