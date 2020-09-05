#pragma once

#include <Core/Buffer.hpp>

#ifdef ENABLE_DEBUG_LAYERS
#define BEGIN_CMD_REGION(cmd, label) cmd->BeginLabel(label)
#define BEGIN_CMD_REGION_COLOR(cmd, label, color) cmd->BeginLabel(label, color)
#define END_CMD_REGION(cmd) cmd->EndLabel()
#else
#define BEGIN_CMD_REGION(cmd, label)
#define END_CMD_REGION(cmd)
#endif

class CommandBuffer {
private:
	enum class CommandBufferState {
		eRecording,
		ePending,
		eDone
	};
	
	vk::CommandBuffer mCommandBuffer;

	void CheckDone();

public:

	STRATUM_API ~CommandBuffer();
	inline operator vk::CommandBuffer() const { return mCommandBuffer; }

	size_t mTriangleCount;
	
	inline ::Device* Device() const { return mDevice; }

	#ifdef ENABLE_DEBUG_LAYERS
	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const std::string& label, const float4& color = float4(1,1,1,0));
	STRATUM_API void EndLabel();
	#endif

	STRATUM_API void Reset(const std::string& name = "Command Buffer");
	STRATUM_API void Wait();

	STRATUM_API void SignalOnComplete(vk::PipelineStageFlags, Semaphore* semaphore) { mSignalSemaphores.push_back(semaphore); };
	STRATUM_API void WaitOn(vk::PipelineStageFlags stage, Semaphore* semaphore) { mWaitSemaphores.push_back(std::make_pair(stage, semaphore)); }

	STRATUM_API stm_ptr<Buffer> GetBuffer(const std::string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API stm_ptr<DescriptorSet> GetDescriptorSet(const std::string& name, vk::DescriptorSetLayout layout);
	STRATUM_API stm_ptr<Texture> GetTexture(const std::string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(stm_ptr<Buffer> resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(stm_ptr<Texture> resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(stm_ptr<DescriptorSet> resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(Framebuffer* resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(RenderPass* resource);

	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier);
	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier);
	STRATUM_API void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier);
	STRATUM_API void Barrier(Texture* texture);
	STRATUM_API void TransitionBarrier(Texture* texture, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, vk::PipelineStageFlags dstStage, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
	STRATUM_API void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

	STRATUM_API void BindDescriptorSet(stm_ptr<DescriptorSet> descriptorSet, uint32_t set);

	STRATUM_API void BeginRenderPass(RenderPass* renderPass, Framebuffer* frameBuffer, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	STRATUM_API void ClearAttachments(const std::vector<vk::ClearAttachment>& value);

	inline RenderPass* CurrentRenderPass() const { return mCurrentRenderPass; }
	inline Framebuffer* CurrentFramebuffer() const { return mCurrentFramebuffer; }
	inline ShaderPassIdentifier CurrentShaderPass() const { return mCurrentShaderPass; }
	inline uint32_t CurrentSubpassIndex() const { return mCurrentSubpassIndex; }

	STRATUM_API void BindPipeline(ComputePipeline* pipeline);
	STRATUM_API void BindPipeline(GraphicsPipeline* pipeline, vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList, const vk::PipelineVertexInputStateCreateInfo& vertexInput = vk::PipelineVertexInputStateCreateInfo(), vk::Optional<const vk::CullModeFlags> cullModeOverride = nullptr, vk::Optional<const vk::PolygonMode> polyModeOverride = nullptr);
	STRATUM_API GraphicsPipeline* BindPipeline(Material* material, Mesh* mesh);

	// Find the range for a push constant (in the current pipeline's layout) named 'name' and push it
	STRATUM_API bool PushConstant(const std::string& name, const void* data, uint32_t dataSize);
	template<typename T>
	inline bool PushConstantRef(const std::string& name, const T& value) { return PushConstant(name, &value, sizeof(T)); }

	STRATUM_API void Dispatch(const uint3& dim);
	inline void Dispatch(const uint2& dim) { Dispatch(uint3(dim, 1)); }
	inline void Dispatch(uint32_t x) { Dispatch(uint3(x, 1, 1)); }
	inline void Dispatch(uint32_t x, uint32_t y) { Dispatch(uint3(x, y, 1)); }
	inline void Dispatch(uint32_t x, uint32_t y, uint32_t z) { Dispatch(uint3(x, y, z)); }

	// Helper to call Dispatch() on ceil(size / workgroupSize)
	STRATUM_API void DispatchAligned(const uint3& size);
	inline void DispatchAligned(const uint2& size) { DispatchAligned(uint3(size, 1)); }
	inline void DispatchAligned(uint32_t x) { DispatchAligned(uint3(x, 1, 1)); }
	inline void DispatchAligned(uint32_t x, uint32_t y) { DispatchAligned(uint3(x, y, 1)); }
	inline void DispatchAligned(uint32_t x, uint32_t y, uint32_t z) { DispatchAligned(uint3(x, y, z)); }

	STRATUM_API void BindVertexBuffer(const BufferView& view, uint32_t index);
	STRATUM_API void BindIndexBuffer(const BufferView& view, vk::IndexType indexType);
	
private:
	friend class Device;
	STRATUM_API CommandBuffer(const std::string& name, ::Device* device, vk::CommandPool commandPool, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void Clear();

	vk::CommandPool mCommandPool;
	::Device* mDevice;
	CommandBufferState mState;
	vk::Fence mSignalFence;

	std::vector<Semaphore*> mSignalSemaphores;
	std::vector<std::pair<vk::PipelineStageFlags, Semaphore*>> mWaitSemaphores;

	// Tracked resources

	std::vector<stm_ptr<Buffer>> mBuffers;
	std::vector<stm_ptr<DescriptorSet>> mDescriptorSets;
	std::vector<stm_ptr<Texture>> mTextures;
	std::vector<stm_ptr<Framebuffer>> mFramebuffers;
	std::vector<stm_ptr<RenderPass>> mRenderPasses;

	// Currently bound objects

	Framebuffer* mCurrentFramebuffer = nullptr;
	RenderPass* mCurrentRenderPass = nullptr;
	uint32_t mCurrentSubpassIndex = 0;
	ShaderPassIdentifier mCurrentShaderPass;
	Material* mBoundMaterial = nullptr;
	PipelineVariant* mBoundVariant = nullptr;
	vk::Pipeline mBoundPipeline;
	vk::PipelineLayout mBoundPipelineLayout;
	std::vector<stm_ptr<DescriptorSet>> mBoundDescriptorSets;
	vk::PipelineBindPoint mCurrentBindPoint;

	std::unordered_map<uint32_t, BufferView> mBoundVertexBuffers;
	BufferView mBoundIndexBuffer;

	#ifdef ENABLE_DEBUG_LAYERS
	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	#endif
};