#include <Core/CommandBuffer.hpp>
#include <Core/Buffer.hpp>
#include <Core/RenderPass.hpp>
#include <Content/Material.hpp>
#include <Content/Shader.hpp>
#include <Content/Texture.hpp>
#include <Scene/Camera.hpp>
#include <Util/Profiler.hpp>
#include <Util/Util.hpp>

#include <Shaders/include/shadercompat.h>

using namespace std;

Semaphore::Semaphore(Device* device) : mDevice(device) {
	VkSemaphoreCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	ThrowIfFailed(vkCreateSemaphore(*mDevice, &info, nullptr, &mSemaphore), "vkCreateSemaphore failed");
}
Semaphore::~Semaphore() {
	vkDestroySemaphore(*mDevice, mSemaphore, nullptr);
}


CommandBuffer::CommandBuffer(::Device* device, VkCommandPool commandPool, const string& name)
	: mDevice(device), mCommandPool(commandPool), mCurrentRenderPass(nullptr), mCurrentMaterial(nullptr), mCurrentPipeline(VK_NULL_HANDLE), mCurrentIndexBuffer(nullptr), mTriangleCount(0) {
	
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	ThrowIfFailed(vkAllocateCommandBuffers(*mDevice, &allocInfo, &mCommandBuffer), "vkAllocateCommandBuffers failed");
	mDevice->SetObjectName(mCommandBuffer, name, VK_OBJECT_TYPE_COMMAND_BUFFER);

	mDevice->mCommandBufferCount++;

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	ThrowIfFailed(vkCreateFence(*mDevice, &fenceInfo, nullptr, &mSignalFence), "vkCreateFence failed");
	mDevice->SetObjectName(mSignalFence, name, VK_OBJECT_TYPE_FENCE);
	
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
	for (uint32_t i = 0; i < mBuffers.size(); i++) mDevice->ReturnToPool(mBuffers[i]);
	for (uint32_t i = 0; i < mDescriptorSets.size(); i++) mDevice->ReturnToPool(mDescriptorSets[i]);
	for (uint32_t i = 0; i < mTextures.size(); i++) mDevice->ReturnToPool(mTextures[i]);
	mBuffers.clear();
	mDescriptorSets.clear();
	mTextures.clear();

	mCurrentRenderPass = nullptr;
	mCurrentCamera = nullptr;
	mCurrentMaterial = nullptr;
	mCurrentPipeline = VK_NULL_HANDLE;
	mTriangleCount = 0;
	mCurrentIndexBuffer = nullptr;
	mCurrentVertexBuffers.clear();
	mSignalSemaphores.clear();
	mWaitSemaphores.clear();
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

// Track a resource, and add it to the resource pool after this commandbuffer finishes executing. The resource will be deleted by the device at a later time.
void CommandBuffer::TrackResource(Buffer* buffer) { mBuffers.push_back(buffer); }
// Track a resource, and add it to the resource pool after this commandbuffer finishes executing. The resource will be deleted by the device at a later time.
void CommandBuffer::TrackResource(Texture* texture) { mTextures.push_back(texture); }
// Track a resource, and add it to the resource pool after this commandbuffer finishes executing. The resource will be deleted by the device at a later time.
void CommandBuffer::TrackResource(DescriptorSet* descriptorSet) { mDescriptorSets.push_back(descriptorSet); }

// TODO: combine barriers
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
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = texture->mLastKnownAccessFlags;
	barrier.dstAccessMask = GuessAccessMask(newLayout);
	barrier.subresourceRange.aspectMask = texture->mAspectFlags;
	Barrier(srcStage, dstStage, barrier);
	texture->mLastKnownLayout = barrier.newLayout;
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

void CommandBuffer::BeginRenderPass(RenderPass* renderPass, const VkRect2D& renderArea, VkFramebuffer frameBuffer, VkClearValue* clearValues, uint32_t clearValueCount) {
	VkRenderPassBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	info.renderPass = *renderPass;
	info.clearValueCount = clearValueCount;
	info.pClearValues = clearValues;
	info.renderArea = renderArea;
	info.framebuffer = frameBuffer;
	vkCmdBeginRenderPass(*this, &info, VK_SUBPASS_CONTENTS_INLINE);

	mCurrentRenderPass = renderPass;

	mTriangleCount = 0;
}
void CommandBuffer::EndRenderPass() {
	vkCmdEndRenderPass(*this);
	mCurrentRenderPass = nullptr;
	mCurrentCamera = nullptr;
	mCurrentMaterial = nullptr;
	mCurrentIndexBuffer = nullptr;
	mCurrentVertexBuffers.clear();
	mCurrentPipeline = VK_NULL_HANDLE;
}

bool CommandBuffer::PushConstant(ShaderVariant* shader, const std::string& name, const void* data, uint32_t dataSize) {
	if (shader->mPushConstants.count(name) == 0) return false;
	VkPushConstantRange range = shader->mPushConstants.at(name);
	vkCmdPushConstants(*this, shader->mPipelineLayout, range.stageFlags, range.offset, min(dataSize, range.size), data);
	return true;
}

VkPipelineLayout CommandBuffer::BindShader(GraphicsShader* shader, PassType pass, const VertexInput* input, Camera* camera, VkPrimitiveTopology topology, VkCullModeFlags cullMode, BlendMode blendMode, VkPolygonMode polyMode) {
	VkPipeline pipeline = shader->GetPipeline(mCurrentRenderPass, input, topology, cullMode, blendMode, polyMode);
	if (pipeline != mCurrentPipeline) {
		vkCmdBindPipeline(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		mCurrentPipeline = pipeline;
		mCurrentCamera = nullptr;
		mCurrentMaterial = nullptr;
	}

	if (camera && mCurrentCamera != camera) {
		if (mCurrentRenderPass && shader->mDescriptorBindings.count("Camera"))
			vkCmdBindDescriptorSets(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->mPipelineLayout, PER_CAMERA, 1, *camera->DescriptorSet(shader->mDescriptorBindings.at("Camera").second.stageFlags), 0, nullptr);
		PushConstantRef(shader, "StereoEye", (uint32_t)EYE_LEFT);
		mCurrentCamera = camera;
	}
	return shader->mPipelineLayout;
}
VkPipelineLayout CommandBuffer::BindMaterial(Material* material, PassType pass, const VertexInput* input, Camera* camera, VkPrimitiveTopology topology, VkCullModeFlags cullMode, BlendMode blendMode, VkPolygonMode polyMode) {
	GraphicsShader* shader = material->GetShader(pass);
	if (!shader) return VK_NULL_HANDLE;

	if (blendMode == BLEND_MODE_MAX_ENUM) blendMode = material->BlendMode();
	if (cullMode == VK_CULL_MODE_FLAG_BITS_MAX_ENUM) cullMode = material->CullMode();

	VkPipeline pipeline = shader->GetPipeline(mCurrentRenderPass, input, topology, cullMode, blendMode, polyMode);
	if (pipeline != mCurrentPipeline) {
		vkCmdBindPipeline(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		mCurrentPipeline = pipeline;
		mCurrentCamera = nullptr;
		mCurrentMaterial = nullptr;
	}

	if (mCurrentCamera != camera || mCurrentMaterial != material) {
		mCurrentCamera = camera;
		mCurrentMaterial = material;
		material->BindDescriptorParameters(this, pass, camera);
	}
	material->SetPushParameter<uint32_t>("StereoEye", 0u);
	material->BindPushParameters(this, pass, camera);

	return shader->mPipelineLayout;
}

void CommandBuffer::BindVertexBuffer(Buffer* buffer, uint32_t index, VkDeviceSize offset) {
	if (mCurrentVertexBuffers[index] == buffer) return;
	VkBuffer buf = buffer == nullptr ? (VkBuffer)VK_NULL_HANDLE : (*buffer);
	vkCmdBindVertexBuffers(mCommandBuffer, index, 1, &buf, &offset);
	mCurrentVertexBuffers[index] = buffer;
}
void CommandBuffer::BindIndexBuffer(Buffer* buffer, VkDeviceSize offset, VkIndexType indexType) {
	if (mCurrentIndexBuffer == buffer) return;
	VkBuffer buf = buffer == nullptr ? (VkBuffer)VK_NULL_HANDLE : (*buffer);
	vkCmdBindIndexBuffer(mCommandBuffer, buf, offset, indexType);
	mCurrentIndexBuffer = buffer;
}