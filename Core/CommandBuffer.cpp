#include <Core/CommandBuffer.hpp>
#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/RenderPass.hpp>
#include <Data/Pipeline.hpp>
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
	CheckDone();
	if (mState == CommandBufferState::ePending)
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
	//for (uint32_t i = 0; i < mFramebuffers.size(); i++) mDevice->PoolResource(mFramebuffers[i]);
	//for (uint32_t i = 0; i < mRenderPasses.size(); i++) mDevice->PoolResource(mRenderPasses[i]);
	mBuffers.clear();
	mDescriptorSets.clear();
	mTextures.clear();
	mFramebuffers.clear();
	mRenderPasses.clear();

	mSignalSemaphores.clear();
	mWaitSemaphores.clear();
	mTriangleCount = 0;

	mCurrentFramebuffer = nullptr;
	mCurrentRenderPass = nullptr;
	mCurrentSubpassIndex = -1;
	mCurrentShaderPass = "";
	mBoundMaterial = nullptr;
	mBoundVariant = nullptr;
	mBoundPipeline = nullptr;
	mBoundPipelineLayout = nullptr;
	mBoundDescriptorSets.clear();

	mBoundIndexBuffer = {};
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
void CommandBuffer::CheckDone() {
	if (mState == CommandBufferState::ePending) {
		vk::Result status = ((vk::Device)*mDevice).getFenceStatus(mSignalFence);
		if (status == vk::Result::eSuccess) mState = CommandBufferState::eDone;
	}
}

stm_ptr<Buffer> CommandBuffer::GetBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	stm_ptr<Buffer> b = mDevice->GetPooledBuffer(name, size, usage, memoryProperties);
	mBuffers.push_back(b);
	return b;
}
stm_ptr<DescriptorSet> CommandBuffer::GetDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout) {
	stm_ptr<DescriptorSet> ds = mDevice->GetPooledDescriptorSet(name, layout);
	mDescriptorSets.push_back(ds);
	return ds;
}
stm_ptr<Texture> CommandBuffer::GetTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels, vk::SampleCountFlagBits sampleCount, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memoryProperties) {
	stm_ptr<Texture> tex = mDevice->GetPooledTexture(name, extent, format, mipLevels, sampleCount, usage, memoryProperties);
	mTextures.push_back(tex);
	return tex;
}

void CommandBuffer::TrackResource(stm_ptr<Buffer> resource) { mBuffers.push_back(resource); }
void CommandBuffer::TrackResource(stm_ptr<Texture> resource) { mTextures.push_back(resource); }
void CommandBuffer::TrackResource(stm_ptr<DescriptorSet> resource) { mDescriptorSets.push_back(resource); }
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

void CommandBuffer::BindDescriptorSet(stm_ptr<DescriptorSet> descriptorSet, uint32_t set) {
	if (!mBoundPipeline) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Error: Cannot bind a descriptor set, without first binding a pipeline\n");
		throw;
	}
	if (mBoundDescriptorSets.size() <= set) mBoundDescriptorSets.resize(set + 1);
	else if (mBoundDescriptorSets[set] == descriptorSet) return;
	descriptorSet->FlushWrites();
	if (!mCurrentRenderPass) descriptorSet->TransitionTextures(this);
	mBoundDescriptorSets[set] = descriptorSet;
	mCommandBuffer.bindDescriptorSets(mCurrentBindPoint, mBoundPipelineLayout, set, { *descriptorSet }, {});
}

void CommandBuffer::BeginRenderPass(RenderPass* renderPass, Framebuffer* framebuffer, vk::SubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->AttachmentCount(); i++)
		TransitionBarrier(framebuffer->Attachment(renderPass->AttachmentName(i)), renderPass->Attachment(i).initialLayout);

	// Assign clear values specified by the render pass
	vector<vk::ClearValue> clearValues(renderPass->mAttachments.size());
	for (const auto& [name, attachment] : renderPass->mSubpasses[0].mAttachments)
		if (attachment.mType == AttachmentType::eDepthStencil)
			clearValues[renderPass->mAttachmentMap.at(name)].depthStencil = { 1.0f, 0 };
		else
			clearValues[renderPass->mAttachmentMap.at(name)].color.setFloat32({ 0.f, 0.f, 0.f, 0.f });

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
		stm_ptr<Texture> attachment = mCurrentFramebuffer->Attachment(mCurrentRenderPass->AttachmentName(i));
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
	if (!mBoundVariant || mBoundVariant->mShaderVariant->mPushConstants.count(name) == 0) return false;
	vk::PushConstantRange range = mBoundVariant->mShaderVariant->mPushConstants.at(name);
	mCommandBuffer.pushConstants(mBoundPipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
	return true;
}

void CommandBuffer::BindPipeline(ComputePipeline* pipeline) {
	if (pipeline->mPipeline == mBoundPipeline) return;
	mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->mPipeline);
	mBoundVariant = pipeline;
	mBoundPipeline = pipeline->mPipeline;
	mBoundPipelineLayout = pipeline->mPipelineLayout;
	mBoundDescriptorSets.clear();
	mBoundMaterial = nullptr;
	mCurrentBindPoint = vk::PipelineBindPoint::eCompute;
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
}
void CommandBuffer::BindPipeline(GraphicsPipeline* pipeline, vk::PrimitiveTopology topology, const vk::PipelineVertexInputStateCreateInfo& vertexInput, vk::Optional<const vk::CullModeFlags> cullModeOverride, vk::Optional<const vk::PolygonMode> polyModeOverride) {
	vk::Pipeline vkpipeline = pipeline->GetPipeline(this, topology, vertexInput, cullModeOverride, polyModeOverride);
	if (vkpipeline == mBoundPipeline) return;
	mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vkpipeline);
	mBoundVariant = pipeline;
	mBoundPipeline = vkpipeline;
	mBoundPipelineLayout = pipeline->mPipelineLayout;
	mBoundDescriptorSets.clear();
	mBoundMaterial = nullptr;
	mCurrentBindPoint = vk::PipelineBindPoint::eGraphics;
	mBoundVertexBuffers.clear();
	mBoundIndexBuffer = {};
}
GraphicsPipeline* CommandBuffer::BindPipeline(Material* material, Mesh* mesh) {
	GraphicsPipeline* pipeline = material->GetPassPipeline(CurrentShaderPass());
	if (!pipeline) return nullptr;

	BindPipeline(pipeline, mesh->Topology(), mesh->PipelineInput(pipeline), material->CullMode(), material->PolygonMode());
	if (mBoundMaterial != material) {
		mBoundMaterial = material;
		material->BindDescriptorParameters(this);
	}
	material->PushConstants(this);
	return pipeline;
}

void CommandBuffer::BindVertexBuffer(const BufferView& view, uint32_t index) {
	if (mBoundVertexBuffers[index] == view) return;
	mCommandBuffer.bindVertexBuffers(index, { (vk::Buffer)*view.mBuffer }, { view.mByteOffset });
	mBoundVertexBuffers[index] = view;
}
void CommandBuffer::BindIndexBuffer(const BufferView& view, vk::IndexType indexType) {
	if (mBoundIndexBuffer == view) return;
	mCommandBuffer.bindIndexBuffer( (vk::Buffer)*view.mBuffer, view.mByteOffset, indexType);
	mBoundIndexBuffer = view;
}

void CommandBuffer::Dispatch(const uint3& dim) {
	mCommandBuffer.dispatch(dim.x, dim.y, dim.z);
}
void CommandBuffer::DispatchAligned(const uint3& dim) {
	ComputePipeline* p = (ComputePipeline*)mBoundVariant;
	Dispatch((dim + p->mShaderVariant->mWorkgroupSize - 1) / p->mShaderVariant->mWorkgroupSize);
}