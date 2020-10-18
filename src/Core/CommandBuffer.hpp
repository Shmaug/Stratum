#pragma once

#include "Pipeline.hpp"
#include "DescriptorSet.hpp"
#include "Framebuffer.hpp"
#include "RenderPass.hpp"

#include "Profiler.hpp"

namespace stm {

class CommandBuffer {
private:
	vk::CommandBuffer mCommandBuffer;

public:
	const std::string mName;
	Device* const mDevice;
	Fence* const mSignalFence;

	CommandBuffer() = delete;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer& operator=(CommandBuffer&&) = delete;
	STRATUM_API ~CommandBuffer();
	inline vk::CommandBuffer operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Device::QueueFamily* QueueFamily() const { return mQueueFamily; }

	STRATUM_API void Reset(const std::string& name = "Command Buffer");

	inline void SignalOnComplete(vk::PipelineStageFlags, std::shared_ptr<Semaphore> semaphore) { mSignalSemaphores.push_back(semaphore); };
	inline void WaitOn(vk::PipelineStageFlags stage, std::shared_ptr<Semaphore> semaphore) { mWaitSemaphores.push_back(std::make_pair(stage, semaphore)); }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const std::string& label, const float4& color = float4(1,1,1,0));
	STRATUM_API void EndLabel();

	STRATUM_API std::shared_ptr<Buffer> GetBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API std::shared_ptr<Texture> GetTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API std::shared_ptr<DescriptorSet> GetDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout);

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(std::shared_ptr<Buffer> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(std::shared_ptr<Texture> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(std::shared_ptr<DescriptorSet> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(std::shared_ptr<Framebuffer> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(std::shared_ptr<RenderPass> resource);

	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier);
	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier);
	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier);
	STRATUM_API void Barrier(Texture& texture);
	STRATUM_API void TransitionBarrier(Texture& texture, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture& texture, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture& texture, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture& texture, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

	STRATUM_API void BindDescriptorSet(std::shared_ptr<DescriptorSet> descriptorSet, uint32_t set);

	STRATUM_API void BeginRenderPass(std::shared_ptr<RenderPass> renderPass, std::shared_ptr<Framebuffer> frameBuffer, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	STRATUM_API void ClearAttachments(const std::vector<vk::ClearAttachment>& value);

	inline std::shared_ptr<RenderPass> CurrentRenderPass() const { return mCurrentRenderPass; }
	inline std::shared_ptr<Framebuffer> CurrentFramebuffer() const { return mCurrentFramebuffer; }
	inline ShaderPassIdentifier CurrentShaderPass() const { return mCurrentShaderPass; }
	inline uint32_t CurrentSubpassIndex() const { return mCurrentSubpassIndex; }
	inline Pipeline* BoundPipeline() const { return mBoundPipeline; }

	STRATUM_API void BindPipeline(ComputePipeline* pipeline);
	STRATUM_API void BindPipeline(GraphicsPipeline* pipeline);
	
	// Find or create a pipeline using the material's shader, and inputMesh's attribute layout
	STRATUM_API GraphicsPipeline* BindPipeline(std::shared_ptr<Material> material, Mesh* inputMesh = nullptr);

	// Find the range for a push constant (in the current pipeline's layout) named 'name' and push it
	STRATUM_API bool PushConstant(const std::string& name, const void* data, uint32_t dataSize);
	template<typename T>
	inline bool PushConstantRef(const std::string& name, const T& value) { return PushConstant(name, &value, sizeof(T)); }

	inline void Dispatch(const uint2& dim) { mCommandBuffer.dispatch(dim.x, dim.y, 1); }
	inline void Dispatch(const uint3& dim) { mCommandBuffer.dispatch(dim.x, dim.y, dim.z); }
	inline void Dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }

	// Helper to call Dispatch() on ceil(size / workgroupSize)
	STRATUM_API void DispatchAligned(const uint2& dim);
	STRATUM_API void DispatchAligned(const uint3& dim);
	inline void DispatchAligned(uint32_t x, uint32_t y = 1, uint32_t z = 1) { DispatchAligned(uint3(x, y, z)); }

	STRATUM_API void BindVertexBuffer(const ArrayBufferView& view, uint32_t index);
	STRATUM_API void BindIndexBuffer(const ArrayBufferView& view);
	
	size_t mTriangleCount;
	
private:
	enum class CommandBufferState { eRecording, eInFlight, eDone };

	friend class Device;
	// assumes a CommandPool has been created for std::this_thread in queueFamily
	STRATUM_API CommandBuffer(const std::string& name, Device* device, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void Clear();

	STRATUM_API bool CheckDone();

	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	
	Device::QueueFamily* mQueueFamily;
	vk::CommandPool mCommandPool;
	CommandBufferState mState;

	std::vector<std::shared_ptr<Semaphore>> mSignalSemaphores;
	std::vector<std::pair<vk::PipelineStageFlags, std::shared_ptr<Semaphore>>> mWaitSemaphores;

	// Tracked resources

	std::vector<std::shared_ptr<Buffer>> mBuffers;
	std::vector<std::shared_ptr<Texture>> mTextures;
	std::vector<std::shared_ptr<DescriptorSet>> mDescriptorSets;
	std::vector<std::shared_ptr<Framebuffer>> mFramebuffers;
	std::vector<std::shared_ptr<RenderPass>> mRenderPasses;

	// Currently bound objects

	std::shared_ptr<Framebuffer> mCurrentFramebuffer;
	std::shared_ptr<RenderPass> mCurrentRenderPass;
	uint32_t mCurrentSubpassIndex = 0;
	ShaderPassIdentifier mCurrentShaderPass;
	std::shared_ptr<Material> mBoundMaterial;
	Pipeline* mBoundPipeline = nullptr;
	std::unordered_map<uint32_t, ArrayBufferView> mBoundVertexBuffers;
	ArrayBufferView mBoundIndexBuffer;
	std::vector<std::shared_ptr<DescriptorSet>> mBoundDescriptorSets;
};

class ProfileRegion {
private:
	CommandBuffer* mCommandBuffer;
public:
	inline ProfileRegion(const std::string& label) : ProfileRegion(label, nullptr) {}
	inline ProfileRegion(const std::string& label, CommandBuffer& cmd) : ProfileRegion(label, &cmd) {}
	inline ProfileRegion(const std::string& label, CommandBuffer* cmd) : mCommandBuffer(cmd) {
		if (mCommandBuffer) mCommandBuffer->BeginLabel(label);
		Profiler::BeginSample(label);
	}
	inline ~ProfileRegion() {
		Profiler::EndSample();
		if (mCommandBuffer) mCommandBuffer->EndLabel();
	}
};

}