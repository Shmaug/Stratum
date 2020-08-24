#include <Core/CommandBuffer.hpp>
#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/Pipeline.hpp>
#include <Core/RenderPass.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>

using namespace std;

CommandBuffer::CommandBuffer( const string& name, ::Device* device, vk::CommandPool commandPool, vk::CommandBufferLevel level) : mDevice(device), mCommandPool(commandPool) {
	#ifdef ENABLE_DEBUG_LAYERS
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr((vk::Instance)*mDevice->Instance(), "vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr((vk::Instance)*mDevice->Instance(), "vkCmdEndDebugUtilsLabelEXT");
	#endif

	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = level;
	allocInfo.commandBufferCount = 1;
	mCommandBuffer = ((vk::Device)*mDevice).allocateCommandBuffers({ allocInfo })[0];
	mDevice->SetObjectName(mCommandBuffer, name);

	mDevice->mCommandBufferCount++;

	mSignalFence = ((vk::Device)*mDevice).createFence({});
	mDevice->SetObjectName(mSignalFence, name);
	
	Clear();

	vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	mCommandBuffer.begin(&beginInfo);
	mState = CommandBufferState::eRecording;
}
CommandBuffer::~CommandBuffer() {
	if (State() == CommandBufferState::ePending)
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Destructing a CommandBuffer that is in CommandBufferState::ePending\n");
	Clear();
	((vk::Device)*mDevice).freeCommandBuffers(mCommandPool, 1, &mCommandBuffer);
	mDevice->Destroy(mSignalFence);
	mDevice->mCommandBufferCount--;
}

#ifdef ENABLE_DEBUG_LAYERS
void CommandBuffer::BeginLabel(const string& text, const float4& color) {
	vk::DebugUtilsLabelEXT label = {};
	memcpy(label.color, &color, sizeof(color));
	label.pLabelName = text.c_str();
	vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, reinterpret_cast<VkDebugUtilsLabelEXT*>(&label));
}
void CommandBuffer::EndLabel() {
	vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
}
#endif

void CommandBuffer::Clear() {
	for (uint32_t i = 0; i < mBuffers.size(); i++) mDevice->PoolResource(mBuffers[i]);
	for (uint32_t i = 0; i < mDescriptorSets.size(); i++) mDevice->PoolResource(mDescriptorSets[i]);
	for (uint32_t i = 0; i < mTextures.size(); i++) mDevice->PoolResource(mTextures[i]);
	for (uint32_t i = 0; i < mFramebuffers.size(); i++) safe_delete(mFramebuffers[i]);
	for (uint32_t i = 0; i < mRenderPasses.size(); i++) safe_delete(mRenderPasses[i]);
	mBuffers.clear();
	mDescriptorSets.clear();
	mTextures.clear();
	mFramebuffers.clear();
	mRenderPasses.clear();

	mSignalSemaphores.clear();
	mWaitSemaphores.clear();
	mTriangleCount = 0;

	mBoundComputePipeline = nullptr;
	mComputePipeline = nullptr;
	mComputePipelineLayout = nullptr;

	mCurrentFramebuffer = nullptr;
	mCurrentRenderPass = nullptr;
	mCurrentSubpassIndex = -1;
	mCurrentShaderPass = "";
	mBoundMaterial = nullptr;
	mBoundGraphicsPipeline = nullptr;
	mGraphicsPipeline = nullptr;
	mGraphicsPipelineLayout = nullptr;

	mBoundGraphicsDescriptorSets.clear();
	mBoundComputeDescriptorSets.clear();

	mBoundIndexBuffer = nullptr;
	mBoundVertexBuffers.clear();
}
void CommandBuffer::Reset(const string& name) {
	mCommandBuffer.reset({});
	mDevice->SetObjectName(mCommandBuffer, name);
	((vk::Device)*mDevice).resetFences({ mSignalFence });
	mDevice->SetObjectName(mSignalFence, name);

	mCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	mState = CommandBufferState::eRecording;

	Clear();
}
void CommandBuffer::Wait() {
	if (mState == CommandBufferState::ePending) {
		((vk::Device)*mDevice).waitForFences({ mSignalFence }, true, numeric_limits<uint64_t>::max());
		Clear();
	}
}
CommandBufferState CommandBuffer::State() {
	if (mState == CommandBufferState::ePending) {
		vk::Result status = ((vk::Device)*mDevice).getFenceStatus(mSignalFence);
		if (status == vk::Result::eSuccess) mState = CommandBufferState::eDone;
	}
	return mState;
}

Buffer* CommandBuffer::GetBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	Buffer* b = mDevice->GetPooledBuffer(name, size, usage, memoryProperties);
	mBuffers.push_back(b);
	return b;
}
DescriptorSet* CommandBuffer::GetDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout) {
	DescriptorSet* ds = mDevice->GetPooledDescriptorSet(name, layout);
	mDescriptorSets.push_back(ds);
	return ds;
}
Texture* CommandBuffer::GetTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits sampleCount, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	Texture* tex = mDevice->GetPooledTexture(name, extent, format, mipLevels, sampleCount, usage, memoryProperties);
	mTextures.push_back(tex);
	return tex;
}

void CommandBuffer::TrackResource(Buffer* resource) { mBuffers.push_back(resource); }
void CommandBuffer::TrackResource(Texture* resource) { mTextures.push_back(resource); }
void CommandBuffer::TrackResource(DescriptorSet* resource) { mDescriptorSets.push_back(resource); }
void CommandBuffer::TrackResource(Framebuffer* resource) { mFramebuffers.push_back(resource); }
void CommandBuffer::TrackResource(RenderPass* resource) { mRenderPasses.push_back(resource); }

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), { barrier }, {}, {});
}
void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, { barrier }, {});
}
void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier) {
	mCommandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, {}, { barrier });
}
void CommandBuffer::Barrier(Texture* texture) {
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = texture->mLastKnownLayout;
	barrier.newLayout = texture->mLastKnownLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *texture;
	barrier.srcAccessMask = texture->mLastKnownAccessFlags;
	barrier.dstAccessMask = texture->mLastKnownAccessFlags;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = texture->mArrayLayers;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = texture->mMipLevels;
	barrier.subresourceRange.aspectMask = texture->mAspectFlags;
	Barrier(texture->mLastKnownStageFlags, texture->mLastKnownStageFlags, barrier);
}
void CommandBuffer::TransitionBarrier(Texture* texture, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, texture->mLastKnownStageFlags, GuessStage(newLayout), texture->mLastKnownLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout) {
	TransitionBarrier(texture, texture->mLastKnownStageFlags, dstStage, texture->mLastKnownLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	if (newLayout == vk::ImageLayout::eUndefined) {
		texture->mLastKnownLayout = newLayout;
		texture->mLastKnownStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
		texture->mLastKnownAccessFlags = {};
		return;
	}
	vk::ImageMemoryBarrier barrier = {};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = *texture;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = texture->MipLevels();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = texture->ArrayLayers();
	barrier.srcAccessMask = texture->mLastKnownAccessFlags;
	barrier.dstAccessMask = GuessAccessMask(newLayout);
	barrier.subresourceRange.aspectMask = texture->mAspectFlags;
	Barrier(srcStage, dstStage, barrier);
	texture->mLastKnownLayout = newLayout;
	texture->mLastKnownStageFlags = dstStage;
	texture->mLastKnownAccessFlags = barrier.dstAccessMask;
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

void CommandBuffer::BindDescriptorSet(DescriptorSet* descriptorSet, uint32_t set) {
	descriptorSet->FlushWrites();
	if (!mCurrentRenderPass) descriptorSet->TransitionTextures(this);
	if (mGraphicsPipeline) {
		if (mBoundGraphicsDescriptorSets.size() <= set) mBoundGraphicsDescriptorSets.resize(set + 1);
		mBoundGraphicsDescriptorSets[set] = descriptorSet;
		mCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mGraphicsPipelineLayout, set, { *descriptorSet }, {});
	} else if (mComputePipeline) {
		if (mBoundComputeDescriptorSets.size() <= set) mBoundComputeDescriptorSets.resize(set + 1);
		mBoundComputeDescriptorSets[set] = descriptorSet;
		mCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, mComputePipelineLayout, set, { *descriptorSet }, {});
	}
}

void CommandBuffer::BeginRenderPass(RenderPass* renderPass, Framebuffer* framebuffer, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->AttachmentCount(); i++)
		TransitionBarrier(framebuffer->Attachment(renderPass->AttachmentName(i)), renderPass->Attachment(i).initialLayout);

	// Assign clear values specified by the render pass
	vector<vk::ClearValue> clearValues(renderPass->mAttachments.size());
	for (const auto& kp : renderPass->mSubpasses[0].mAttachments)
		if (kp.second.mType == ATTACHMENT_DEPTH_STENCIL)
			clearValues[renderPass->mAttachmentMap.at(kp.first)].depthStencil = { 1.0f, 0 };
		else
			clearValues[renderPass->mAttachmentMap.at(kp.first)].color.setFloat32({ 0.f, 0.f, 0.f, 0.f });

	vk::RenderPassBeginInfo info = {};
	info.renderPass = *renderPass;
	info.framebuffer = *framebuffer;
	info.renderArea = { { 0, 0 }, framebuffer->Extent() };
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
		Texture* attachment = mCurrentFramebuffer->Attachment(mCurrentRenderPass->AttachmentName(i));
		attachment->mLastKnownLayout = mCurrentRenderPass->Attachment(i).finalLayout;
		attachment->mLastKnownStageFlags = GuessStage(mCurrentRenderPass->Attachment(i).finalLayout);
		attachment->mLastKnownAccessFlags = GuessAccessMask(mCurrentRenderPass->Attachment(i).finalLayout);
	}
	
	mCurrentRenderPass = nullptr;
	mCurrentFramebuffer = nullptr;
	mCurrentShaderPass = "";
	mCurrentSubpassIndex = -1;
}

void CommandBuffer::ClearAttachments(const vector<vk::ClearAttachment>& values) {
	vk::ClearRect rect = {};
	rect.layerCount = 1;
	rect.rect = { {}, mCurrentFramebuffer->Extent() } ;
	mCommandBuffer.clearAttachments((uint32_t)values.size(), values.data(), 1, &rect);
}

bool CommandBuffer::PushConstant(const std::string& name, const void* data, uint32_t dataSize) {
	if (mBoundGraphicsPipeline) {
		if (mBoundGraphicsPipeline->mShaderVariant->mPushConstants.count(name) == 0) return false;
		vk::PushConstantRange range = mBoundGraphicsPipeline->mShaderVariant->mPushConstants.at(name);
		mCommandBuffer.pushConstants(mGraphicsPipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
		return true;
	} else if (mBoundComputePipeline) {
		if (mBoundComputePipeline->mShaderVariant->mPushConstants.count(name) == 0) return false;
		vk::PushConstantRange range = mBoundComputePipeline->mShaderVariant->mPushConstants.at(name);
		mCommandBuffer.pushConstants(mComputePipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
		return true;
	}
	return false;
}

void CommandBuffer::BindPipeline(ComputePipeline* pipeline) {
	if (pipeline->mPipeline == mComputePipeline) return;
	mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->mPipeline);
	mComputePipeline = pipeline->mPipeline;
	mComputePipelineLayout = pipeline->mPipelineLayout;
	mBoundComputePipeline = pipeline;
	mBoundComputeDescriptorSets.clear();

	mGraphicsPipeline = nullptr;
	mGraphicsPipelineLayout = nullptr;
	mBoundGraphicsPipeline = nullptr;
	mBoundMaterial = nullptr;
	mBoundGraphicsDescriptorSets.clear();

}
void CommandBuffer::BindPipeline(GraphicsPipeline* pipeline, const VertexInput* input, vk::PrimitiveTopology topology, vk::Optional<const vk::CullModeFlags> cullModeOverride, vk::Optional<const vk::PolygonMode> polyModeOverride) {
	vk::Pipeline vkpipeline = pipeline->GetPipeline(this, input, topology, cullModeOverride, polyModeOverride);
	if (vkpipeline == mGraphicsPipeline) return;
	mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vkpipeline);
	mComputePipeline = nullptr;
	mComputePipelineLayout = nullptr;
	mBoundComputePipeline = nullptr;
	mBoundComputeDescriptorSets.clear();

	mGraphicsPipeline = vkpipeline;
	mGraphicsPipelineLayout = pipeline->mPipelineLayout;
	mBoundGraphicsPipeline = pipeline;
	mBoundMaterial = nullptr;
	mBoundGraphicsDescriptorSets.clear();
}
bool CommandBuffer::BindMaterial(Material* material, const VertexInput* vertexInput, vk::PrimitiveTopology topology) {
	GraphicsPipeline* pipeline = material->GetPassPipeline(CurrentShaderPass());
	if (!pipeline) return false;
	BindPipeline(pipeline, vertexInput, topology, material->CullMode(), material->PolygonMode());
	if (mBoundMaterial != material) {
		mBoundMaterial = material;
		material->BindDescriptorParameters(this);
	}
	material->PushConstants(this);
	return true;
}

void CommandBuffer::BindVertexBuffer(Buffer* buffer, uint32_t index, vk::DeviceSize offset) {
	if (mBoundVertexBuffers[index] == buffer) return;
	vk::Buffer buf = nullptr;
	if (buffer) buf = *buffer;
	mCommandBuffer.bindVertexBuffers(index, 1, &buf, &offset);
	mBoundVertexBuffers[index] = buffer;
}
void CommandBuffer::BindIndexBuffer(Buffer* buffer, vk::DeviceSize offset, vk::IndexType indexType) {
	if (mBoundIndexBuffer == buffer) return;
	vk::Buffer buf = nullptr;
	if (buffer) buf = *buffer;
	mCommandBuffer.bindIndexBuffer(buf, offset, indexType);
	mBoundIndexBuffer = buffer;
}

void CommandBuffer::Dispatch(const uint3& dim) {
	mCommandBuffer.dispatch(dim.x, dim.y, dim.z);
}
void CommandBuffer::DispatchAligned(const uint3& dim) {
	if (!mBoundComputePipeline) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Calling DispatchAligned without any compute pipeline bound\n");
		throw;
	}
	Dispatch((dim + mBoundComputePipeline->mShaderVariant->mWorkgroupSize - 1) / mBoundComputePipeline->mShaderVariant->mWorkgroupSize);
}