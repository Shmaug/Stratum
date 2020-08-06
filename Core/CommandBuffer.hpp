#pragma once

#include <Core/Device.hpp>

#ifdef ENABLE_DEBUG_LAYERS
#define BEGIN_CMD_REGION(cmd, label) cmd->BeginLabel(label)
#define BEGIN_CMD_REGION_COLOR(cmd, label, color) cmd->BeginLabel(label, color)
#define END_CMD_REGION(cmd) cmd->EndLabel()
#else
#define BEGIN_CMD_REGION(cmd, label)
#define END_CMD_REGION(cmd)
#endif

class Semaphore {
public:
	STRATUM_API Semaphore(Device* device);
	STRATUM_API ~Semaphore();
	inline operator VkSemaphore() const { return mSemaphore; }
private:
	Device* mDevice;
	VkSemaphore mSemaphore;
};

class CommandBuffer {
public:
	STRATUM_API ~CommandBuffer();
	inline operator VkCommandBuffer() const { return mCommandBuffer; }

	size_t mTriangleCount;
	
	inline ::Device* Device() const { return mDevice; }

	#ifdef ENABLE_DEBUG_LAYERS
	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const std::string& label, const float4& color = float4(1,1,1,0));
	STRATUM_API void EndLabel();
	#endif

	STRATUM_API void Reset(const std::string& name = "Command Buffer");
	STRATUM_API void Wait();
	inline CommandBufferState State();

	STRATUM_API void Signal(VkPipelineStageFlags, Semaphore* semaphore) { mSignalSemaphores.push_back(semaphore); };
	STRATUM_API void WaitOn(VkPipelineStageFlags stage, Semaphore* semaphore) { mWaitSemaphores.push_back(std::make_pair(stage, semaphore)); }

	STRATUM_API Buffer* GetBuffer(const std::string& name, VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	STRATUM_API DescriptorSet* GetDescriptorSet(const std::string& name, VkDescriptorSetLayout layout);
	STRATUM_API Texture* GetTexture(const std::string& name, const VkExtent3D& extent, VkFormat format, uint32_t mipLevels = 1, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(Buffer* resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(Texture* resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(DescriptorSet* resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(Framebuffer* resource);
	// Track a resource, and add it to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(RenderPass* resource);

	STRATUM_API void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkMemoryBarrier& barrier);
	STRATUM_API void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkImageMemoryBarrier& barrier);
	STRATUM_API void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkBufferMemoryBarrier& barrier);
	STRATUM_API void Barrier(Texture* texture);
	STRATUM_API void TransitionBarrier(Texture* texture, VkImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, VkImageLayout oldLayout, VkImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, VkPipelineStageFlags dstStage, VkImageLayout newLayout);
	STRATUM_API void TransitionBarrier(Texture* texture, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout);
	STRATUM_API void TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout);
	STRATUM_API void TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout);

	STRATUM_API void BindDescriptorSet(DescriptorSet* descriptorSet, uint32_t set);

	STRATUM_API void BeginRenderPass(RenderPass* renderPass, Framebuffer* frameBuffer, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
	STRATUM_API void NextSubpass(VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
	STRATUM_API void EndRenderPass();

	STRATUM_API void ClearAttachments(const std::vector<VkClearAttachment>& value);

	inline RenderPass* CurrentRenderPass() const { return mCurrentRenderPass; }
	inline Framebuffer* CurrentFramebuffer() const { return mCurrentFramebuffer; }
	inline ShaderPassIdentifier CurrentShaderPass() const { return mCurrentShaderPass; }
	inline uint32_t CurrentSubpassIndex() const { return mCurrentSubpassIndex; }

	STRATUM_API void BindPipeline(ComputePipeline* pipeline);
	STRATUM_API void BindPipeline(GraphicsPipeline* shader, const VertexInput* vertexInput = nullptr, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VkCullModeFlags cullModeOverride = VK_CULL_MODE_FLAG_BITS_MAX_ENUM, VkPolygonMode polyModeOverride = VK_POLYGON_MODE_MAX_ENUM);
	STRATUM_API bool BindMaterial(Material* material, const VertexInput* vertexInput = nullptr, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

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

	STRATUM_API void BindVertexBuffer(Buffer* buffer, uint32_t index, VkDeviceSize offset);
	STRATUM_API void BindIndexBuffer(Buffer* buffer, VkDeviceSize offset, VkIndexType indexType);
	
private:
	friend class Device;
	STRATUM_API CommandBuffer(const std::string& name, ::Device* device, VkCommandPool commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	
	STRATUM_API void Clear();

	::Device* mDevice;
	VkCommandPool mCommandPool;
	VkCommandBuffer mCommandBuffer;
	CommandBufferState mState;
	VkFence mSignalFence;

	std::vector<Semaphore*> mSignalSemaphores;
	std::vector<std::pair<VkPipelineStageFlags, Semaphore*>> mWaitSemaphores;

	// Tracked resources

	std::vector<Buffer*> mBuffers;
	std::vector<DescriptorSet*> mDescriptorSets;
	std::vector<Texture*> mTextures;
	std::vector<Framebuffer*> mFramebuffers;
	std::vector<RenderPass*> mRenderPasses;

	// Currently bound objects

	ComputePipeline* mBoundComputePipeline;
	VkPipeline mComputePipeline;
	VkPipelineLayout mComputePipelineLayout;
	std::vector<DescriptorSet*> mBoundComputeDescriptorSets;

	Framebuffer* mCurrentFramebuffer;
	RenderPass* mCurrentRenderPass;
	uint32_t mCurrentSubpassIndex;
	ShaderPassIdentifier mCurrentShaderPass;
	Material* mBoundMaterial;
	GraphicsPipeline* mBoundGraphicsPipeline;
	VkPipeline mGraphicsPipeline;
	VkPipelineLayout mGraphicsPipelineLayout;
	std::vector<DescriptorSet*> mBoundGraphicsDescriptorSets;

	std::unordered_map<uint32_t, Buffer*> mBoundVertexBuffers;
	Buffer* mBoundIndexBuffer;
};