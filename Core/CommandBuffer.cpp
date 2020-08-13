#include <Core/CommandBuffer.hpp>
#include <Core/Buffer.hpp>
#include <Core/RenderPass.hpp>
#include <Core/Framebuffer.hpp>
#include <Data/Material.hpp>
#include <Core/Pipeline.hpp>
#include <Data/Texture.hpp>
#include <Scene/Camera.hpp>
#include <Util/Profiler.hpp>
#include <Util/Util.hpp>

using namespace std;

Semaphore::Semaphore(Device* device) : mDevice(device) {
	VkSemaphoreCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	ThrowIfFailed(vkCreateSemaphore(*mDevice, &info, nullptr, &mSemaphore), "vkCreateSemaphore failed");
}
Semaphore::~Semaphore() {
	vkDestroySemaphore(*mDevice, mSemaphore, nullptr);
}


CommandBuffer::CommandBuffer( const string& name, ::Device* device, VkCommandPool commandPool, VkCommandBufferLevel level) : mDevice(device), mCommandPool(commandPool) {
	
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = level;
	allocInfo.commandBufferCount = 1;
	ThrowIfFailed(vkAllocateCommandBuffers(*mDevice, &allocInfo, &mCommandBuffer), "vkAllocateCommandBuffers failed");
	mDevice->SetObjectName(mCommandBuffer, name, VK_OBJECT_TYPE_COMMAND_BUFFER);

	mDevice->mCommandBufferCount++;

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	ThrowIfFailed(vkCreateFence(*mDevice, &fenceInfo, nullptr, &mSignalFence), "vkCreateFence failed");
	mDevice->SetObjectName(mSignalFence, name, VK_OBJECT_TYPE_FENCE);
	
	Clear();

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	ThrowIfFailed(vkBeginCommandBuffer(mCommandBuffer, &beginInfo), "vkBeginCommandBuffer failed");
	mState = CMDBUF_STATE_RECORDING;
}
CommandBuffer::~CommandBuffer() {
	if (State() == CMDBUF_STATE_PENDING)
		fprintf_color(COLOR_YELLOW, stderr, "Warning: Destructing a CommandBuffer that is in CMDBUF_STATE_PENDING\n");
	Clear();
	vkFreeCommandBuffers(*mDevice, mCommandPool, 1, &mCommandBuffer);
	vkDestroyFence(*mDevice, mSignalFence, nullptr);
	mDevice->mCommandBufferCount--;
}

#ifdef ENABLE_DEBUG_LAYERS
void CommandBuffer::BeginLabel(const string& text, const float4& color) {
	VkDebugUtilsLabelEXT label = {};
	label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	memcpy(label.color, &color, sizeof(color));
	label.pLabelName = text.c_str();
	mDevice->CmdBeginDebugUtilsLabelEXT(mCommandBuffer, &label);
}
void CommandBuffer::EndLabel() {
	mDevice->CmdEndDebugUtilsLabelEXT(mCommandBuffer);
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
	mComputePipeline = VK_NULL_HANDLE;
	mComputePipelineLayout = VK_NULL_HANDLE;

	mCurrentFramebuffer = nullptr;
	mCurrentRenderPass = nullptr;
	mCurrentSubpassIndex = -1;
	mCurrentShaderPass = "";
	mBoundMaterial = nullptr;
	mBoundGraphicsPipeline = nullptr;
	mGraphicsPipeline = VK_NULL_HANDLE;
	mGraphicsPipelineLayout = VK_NULL_HANDLE;

	mBoundGraphicsDescriptorSets.clear();
	mBoundComputeDescriptorSets.clear();

	mBoundIndexBuffer = nullptr;
	mBoundVertexBuffers.clear();
}
void CommandBuffer::Reset(const string& name) {
	ThrowIfFailed(vkResetCommandBuffer(mCommandBuffer, 0), "vkResetCommandBuffer failed");
	mDevice->SetObjectName(mCommandBuffer, name, VK_OBJECT_TYPE_COMMAND_BUFFER);
	ThrowIfFailed(vkResetFences(*mDevice, 1, &mSignalFence), "vkResetFences failed");
	mDevice->SetObjectName(mSignalFence, name, VK_OBJECT_TYPE_FENCE);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	ThrowIfFailed(vkBeginCommandBuffer(mCommandBuffer, &beginInfo), "vkBeginCommandBuffer failed");
	mState = CMDBUF_STATE_RECORDING;

	Clear();
}
void CommandBuffer::Wait() {
	if (mState == CMDBUF_STATE_PENDING) {
		ThrowIfFailed(vkWaitForFences(*mDevice, 1, &mSignalFence, true, numeric_limits<uint64_t>::max()), "vkWaitForFences failed");
		Clear();
	}
}
CommandBufferState CommandBuffer::State() {
	if (mState == CMDBUF_STATE_PENDING) {
		VkResult status = vkGetFenceStatus(*mDevice, mSignalFence);
		if (status == VK_SUCCESS) {
			mState = CMDBUF_STATE_DONE;
			return mState;
		} 
		else if (status == VK_NOT_READY) return mState;
		else ThrowIfFailed(status, "vkGetFenceStatus failed");
	}
	return mState;
}

Buffer* CommandBuffer::GetBuffer(const std::string& name, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties) {
	Buffer* b = mDevice->GetPooledBuffer(name, size, usage, memoryProperties);
	mBuffers.push_back(b);
	return b;
}
DescriptorSet* CommandBuffer::GetDescriptorSet(const std::string& name, VkDescriptorSetLayout layout) {
	DescriptorSet* ds = mDevice->GetPooledDescriptorSet(name, layout);
	mDescriptorSets.push_back(ds);
	return ds;
}
Texture* CommandBuffer::GetTexture(const std::string& name, const VkExtent3D& extent, VkFormat format, uint32_t mipLevels, VkSampleCountFlagBits sampleCount, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryProperties) {
	Texture* tex = mDevice->GetPooledTexture(name, extent, format, mipLevels, sampleCount, usage, memoryProperties);
	mTextures.push_back(tex);
	return tex;
}

void CommandBuffer::TrackResource(Buffer* resource) { mBuffers.push_back(resource); }
void CommandBuffer::TrackResource(Texture* resource) { mTextures.push_back(resource); }
void CommandBuffer::TrackResource(DescriptorSet* resource) { mDescriptorSets.push_back(resource); }
void CommandBuffer::TrackResource(Framebuffer* resource) { mFramebuffers.push_back(resource); }
void CommandBuffer::TrackResource(RenderPass* resource) { mRenderPasses.push_back(resource); }

void CommandBuffer::Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkMemoryBarrier& barrier) {
	vkCmdPipelineBarrier(mCommandBuffer, srcStage, dstStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}
void CommandBuffer::Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkBufferMemoryBarrier& barrier) {
	vkCmdPipelineBarrier(mCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}
void CommandBuffer::Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkImageMemoryBarrier& barrier) {
	vkCmdPipelineBarrier(mCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void CommandBuffer::Barrier(Texture* texture) {
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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
void CommandBuffer::TransitionBarrier(Texture* texture, VkImageLayout newLayout) {
	TransitionBarrier(texture, texture->mLastKnownStageFlags, GuessStage(newLayout), texture->mLastKnownLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, VkImageLayout oldLayout, VkImageLayout newLayout) {
	TransitionBarrier(texture, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, VkPipelineStageFlags dstStage, VkImageLayout newLayout) {
	TransitionBarrier(texture, texture->mLastKnownStageFlags, dstStage, texture->mLastKnownLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(Texture* texture, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	if (newLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
		texture->mLastKnownLayout = newLayout;
		texture->mLastKnownStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		texture->mLastKnownAccessFlags = 0;
		return;
	}
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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
void CommandBuffer::TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout) {
	TransitionBarrier(image, subresourceRange, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
}
void CommandBuffer::TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout) {
	if (oldLayout == newLayout) return;
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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
	if (mGraphicsPipeline != VK_NULL_HANDLE) {
		if (mBoundGraphicsDescriptorSets.size() <= set) mBoundGraphicsDescriptorSets.resize(set + 1);
		mBoundGraphicsDescriptorSets[set] = descriptorSet;
		vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mGraphicsPipelineLayout, set, 1, *descriptorSet, 0, nullptr);
	} else if (mComputePipeline != VK_NULL_HANDLE) {
		if (mBoundComputeDescriptorSets.size() <= set) mBoundComputeDescriptorSets.resize(set + 1);
		mBoundComputeDescriptorSets[set] = descriptorSet;
		vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mComputePipelineLayout, set, 1, *descriptorSet, 0, nullptr);
	}
}

void CommandBuffer::BeginRenderPass(RenderPass* renderPass, Framebuffer* framebuffer, VkSubpassContents contents) {
	// Transition attachments to the layouts specified by the render pass
	// Image states are untracked during a renderpass
	for (uint32_t i = 0; i < renderPass->AttachmentCount(); i++)
		TransitionBarrier(framebuffer->Attachment(renderPass->AttachmentName(i)), renderPass->Attachment(i).initialLayout);

	// Assign clear values specified by the render pass
	vector<VkClearValue> clearValues(renderPass->mAttachments.size());
	for (const auto& kp : renderPass->mSubpasses[0].mAttachments)
		if (kp.second.mType == ATTACHMENT_DEPTH_STENCIL)
			clearValues[renderPass->mAttachmentMap.at(kp.first)].depthStencil = { 1.0f, 0 };
		else
			clearValues[renderPass->mAttachmentMap.at(kp.first)].color = { 0.f, 0.f, 0.f, 0.f };

	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.renderPass = *renderPass;
	info.framebuffer = *framebuffer;
	info.renderArea = { { 0, 0 }, framebuffer->Extent() };
	info.clearValueCount = (uint32_t)clearValues.size();
	info.pClearValues = clearValues.data();
	vkCmdBeginRenderPass(mCommandBuffer, &info, contents);

	mCurrentRenderPass = renderPass;
	mCurrentFramebuffer = framebuffer;
	mCurrentSubpassIndex = 0;
	mCurrentShaderPass = renderPass->mSubpasses[0].mShaderPass;
}
void CommandBuffer::NextSubpass(VkSubpassContents contents) {
	vkCmdNextSubpass(mCommandBuffer, contents);
	mCurrentSubpassIndex++;
	mCurrentShaderPass = mCurrentRenderPass->mSubpasses[mCurrentSubpassIndex].mShaderPass;
}
void CommandBuffer::EndRenderPass() {
	vkCmdEndRenderPass(mCommandBuffer);

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

void CommandBuffer::ClearAttachments(const vector<VkClearAttachment>& values) {
	VkClearRect rect = {};
	rect.layerCount = 1;
	rect.rect = { {}, mCurrentFramebuffer->Extent() } ;
	vkCmdClearAttachments(mCommandBuffer, (uint32_t)values.size(), values.data(), 1, &rect);
}

bool CommandBuffer::PushConstant(const std::string& name, const void* data, uint32_t dataSize) {
	if (mBoundGraphicsPipeline) {
		if (mBoundGraphicsPipeline->mShaderVariant->mPushConstants.count(name) == 0) return false;
		VkPushConstantRange range = mBoundGraphicsPipeline->mShaderVariant->mPushConstants.at(name);
		vkCmdPushConstants(mCommandBuffer, mGraphicsPipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
		return true;
	} else if (mBoundComputePipeline) {
		if (mBoundComputePipeline->mShaderVariant->mPushConstants.count(name) == 0) return false;
		VkPushConstantRange range = mBoundComputePipeline->mShaderVariant->mPushConstants.at(name);
		vkCmdPushConstants(mCommandBuffer, mComputePipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
		return true;
	}
	return false;
}

void CommandBuffer::BindPipeline(ComputePipeline* pipeline) {
	if (pipeline->mPipeline == mComputePipeline) return;
	vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->mPipeline);
	mComputePipeline = pipeline->mPipeline;
	mComputePipelineLayout = pipeline->mPipelineLayout;
	mBoundComputePipeline = pipeline;
	mBoundComputeDescriptorSets.clear();

	mGraphicsPipeline = VK_NULL_HANDLE;
	mGraphicsPipelineLayout = VK_NULL_HANDLE;
	mBoundGraphicsPipeline = nullptr;
	mBoundMaterial = nullptr;
	mBoundGraphicsDescriptorSets.clear();

}
void CommandBuffer::BindPipeline(GraphicsPipeline* pipeline, const VertexInput* input, VkPrimitiveTopology topology, VkCullModeFlags cullMode, VkPolygonMode polyMode) {
	VkPipeline vkpipeline = pipeline->GetPipeline(this, input, topology, cullMode, polyMode);
	if (vkpipeline == mGraphicsPipeline) return;
	vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkpipeline);
	mComputePipeline = VK_NULL_HANDLE;
	mComputePipelineLayout = VK_NULL_HANDLE;
	mBoundComputePipeline = nullptr;
	mBoundComputeDescriptorSets.clear();

	mGraphicsPipeline = vkpipeline;
	mGraphicsPipelineLayout = pipeline->mPipelineLayout;
	mBoundGraphicsPipeline = pipeline;
	mBoundMaterial = nullptr;
	mBoundGraphicsDescriptorSets.clear();
}
bool CommandBuffer::BindMaterial(Material* material, const VertexInput* vertexInput, VkPrimitiveTopology topology) {
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

void CommandBuffer::BindVertexBuffer(Buffer* buffer, uint32_t index, VkDeviceSize offset) {
	if (mBoundVertexBuffers[index] == buffer) return;
	VkBuffer buf = buffer == nullptr ? (VkBuffer)VK_NULL_HANDLE : (*buffer);
	vkCmdBindVertexBuffers(mCommandBuffer, index, 1, &buf, &offset);
	mBoundVertexBuffers[index] = buffer;
}
void CommandBuffer::BindIndexBuffer(Buffer* buffer, VkDeviceSize offset, VkIndexType indexType) {
	if (mBoundIndexBuffer == buffer) return;
	VkBuffer buf = buffer == nullptr ? (VkBuffer)VK_NULL_HANDLE : (*buffer);
	vkCmdBindIndexBuffer(mCommandBuffer, buf, offset, indexType);
	mBoundIndexBuffer = buffer;
}

void CommandBuffer::Dispatch(const uint3& dim) {
	vkCmdDispatch(mCommandBuffer, dim.x, dim.y, dim.z);
}
void CommandBuffer::DispatchAligned(const uint3& dim) {
	if (!mBoundComputePipeline) {
		fprintf_color(COLOR_RED, stderr, "Error: Calling DispatchAligned without any compute pipeline bound\n");
		throw;
	}
	Dispatch((dim + mBoundComputePipeline->mShaderVariant->mWorkgroupSize - 1) / mBoundComputePipeline->mShaderVariant->mWorkgroupSize);
}