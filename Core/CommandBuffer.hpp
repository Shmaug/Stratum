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
	ENGINE_EXPORT Semaphore(Device* device);
	ENGINE_EXPORT ~Semaphore();
	inline operator VkSemaphore() const { return mSemaphore; }
private:
	Device* mDevice;
	VkSemaphore mSemaphore;
};

class CommandBuffer {
public:
	ENGINE_EXPORT ~CommandBuffer();
	inline operator VkCommandBuffer() const { return mCommandBuffer; }

	size_t mTriangleCount;
	
	inline RenderPass* CurrentRenderPass() const { return mCurrentRenderPass; }
	inline ::Device* Device() const { return mDevice; }

	#ifdef ENABLE_DEBUG_LAYERS
	// Label a region for a tool such as RenderDoc
	ENGINE_EXPORT void BeginLabel(const std::string& label, const float4& color = float4(1,1,1,0));
	ENGINE_EXPORT void EndLabel();
	#endif

	ENGINE_EXPORT void Reset(const std::string& name = "Command Buffer");
	ENGINE_EXPORT void Wait();
	inline CommandBufferState State();

	ENGINE_EXPORT void Signal(VkPipelineStageFlags, Semaphore* semaphore) { mSignalSemaphores.push_back(semaphore); };
	ENGINE_EXPORT void WaitOn(VkPipelineStageFlags stage, Semaphore* semaphore) { mWaitSemaphores.push_back(std::make_pair(stage, semaphore)); }

	ENGINE_EXPORT Buffer* GetBuffer(const std::string& name, VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ENGINE_EXPORT DescriptorSet* GetDescriptorSet(const std::string& name, VkDescriptorSetLayout layout);
	ENGINE_EXPORT Texture* GetTexture(const std::string& name, const VkExtent3D& extent, VkFormat format, uint32_t mipLevels = 1, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT, VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Track a resource, and add it to the resource pool after this commandbuffer finishes executing
	ENGINE_EXPORT void TrackResource(Buffer* buffer);
	// Track a resource, and add it to the resource pool after this commandbuffer finishes executing
	ENGINE_EXPORT void TrackResource(Texture* texture);
	// Track a resource, and add it to the resource pool after this commandbuffer finishes executing
	ENGINE_EXPORT void TrackResource(DescriptorSet* descriptorSet);

	ENGINE_EXPORT void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkMemoryBarrier& barrier);
	ENGINE_EXPORT void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkImageMemoryBarrier& barrier);
	ENGINE_EXPORT void Barrier(VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, const VkBufferMemoryBarrier& barrier);
	ENGINE_EXPORT void Barrier(Texture* texture);
	ENGINE_EXPORT void TransitionBarrier(Texture* texture, VkImageLayout newLayout);
	ENGINE_EXPORT void TransitionBarrier(Texture* texture, VkImageLayout oldLayout, VkImageLayout newLayout);
	ENGINE_EXPORT void TransitionBarrier(Texture* texture, VkPipelineStageFlags dstStage, VkImageLayout newLayout);
	ENGINE_EXPORT void TransitionBarrier(Texture* texture, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout);
	ENGINE_EXPORT void TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout);
	ENGINE_EXPORT void TransitionBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout oldLayout, VkImageLayout newLayout);

	ENGINE_EXPORT void BeginRenderPass(RenderPass* renderPass, Framebuffer* frameBuffer);
	ENGINE_EXPORT void EndRenderPass();

	ENGINE_EXPORT void BindVertexBuffer(Buffer* buffer, uint32_t index, VkDeviceSize offset);
	ENGINE_EXPORT void BindIndexBuffer(Buffer* buffer, VkDeviceSize offset, VkIndexType indexType);

	// Binds a shader pipeline, if it is not already bound
	// If camera is not nullptr, attempts to bind the camera's parameters
	ENGINE_EXPORT VkPipelineLayout BindShader(GraphicsShader* shader, PassType pass, const VertexInput* input, Camera* camera = nullptr,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM,
		BlendMode blendMode = BLEND_MODE_MAX_ENUM,
		VkPolygonMode polyMode = VK_POLYGON_MODE_MAX_ENUM);

	// Binds a material pipeline and sets its parameters, if it is not already bound
	// If camera is not nullptr, attempts to bind the camera's parameters
	ENGINE_EXPORT VkPipelineLayout BindMaterial(Material* material, PassType pass, const VertexInput* input, Camera* camera = nullptr,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM,
		BlendMode blendMode = BLEND_MODE_MAX_ENUM,
		VkPolygonMode polyMode = VK_POLYGON_MODE_MAX_ENUM);

	// Find the range for a push constant (in the current shader's layout) named 'name' and push it
	ENGINE_EXPORT bool PushConstant(const std::string& name, const void* data, uint32_t dataSize);
	template<typename T>
	inline bool PushConstantRef(const std::string& name, const T& value) { return PushConstant(name, &value, sizeof(T)); }

private:
	friend class Stratum;
	friend class Device;
	ENGINE_EXPORT CommandBuffer(::Device* device, VkCommandPool commandPool, const std::string& name = "Command Buffer");
	
	ENGINE_EXPORT void Clear();

	::Device* mDevice;
	VkCommandPool mCommandPool;
	VkCommandBuffer mCommandBuffer;
	CommandBufferState mState;
	VkFence mSignalFence;

	// In-flight resources

	std::vector<Buffer*> mBuffers;
	std::vector<DescriptorSet*> mDescriptorSets;
	std::vector<Texture*> mTextures;

	std::vector<Semaphore*> mSignalSemaphores;
	std::vector<std::pair<VkPipelineStageFlags, Semaphore*>> mWaitSemaphores;

	// Currently bound objects

	VkPipeline mCurrentPipeline;
	std::unordered_map<uint32_t, Buffer*> mCurrentVertexBuffers;
	Buffer* mCurrentIndexBuffer;
	RenderPass* mCurrentRenderPass;
	Camera* mCurrentCamera;
	Material* mCurrentMaterial;
	GraphicsShader* mCurrentShader;
};