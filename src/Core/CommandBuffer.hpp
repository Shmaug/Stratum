#pragma once

#include "DescriptorSet.hpp"
#include "Framebuffer.hpp"
#include "Pipeline.hpp"
#include "Profiler.hpp"

namespace stm {

class CommandBuffer {
public:
	size_t mTriangleCount;
	
	CommandBuffer() = delete;
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) = delete;
	CommandBuffer& operator=(const CommandBuffer&) = delete;
	CommandBuffer& operator=(CommandBuffer&&) = delete;
	STRATUM_API ~CommandBuffer();
	inline vk::CommandBuffer operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Device& Device() const { return mDevice; }
	inline Fence* SignalFence() const { return mSignalFence; }
	inline stm::QueueFamily* QueueFamily() const { return mQueueFamily; }

	inline shared_ptr<RenderPass> CurrentRenderPass() const { return mCurrentRenderPass; }
	inline shared_ptr<Framebuffer> CurrentFramebuffer() const { return mCurrentFramebuffer; }
	inline shared_ptr<Pipeline> BoundPipeline() const { return mBoundPipeline; }
	inline const Subpass& CurrentSubpass() const { return mCurrentRenderPass->Subpass(mCurrentSubpassIndex); }
	inline uint32_t CurrentSubpassIndex() const { return mCurrentSubpassIndex; }
	inline ShaderPassIdentifier CurrentShaderPass() const { return mCurrentShaderPass; }

	STRATUM_API void Reset(const string& name = "Command Buffer");

	inline void SignalOnComplete(vk::PipelineStageFlags, shared_ptr<Semaphore> semaphore) { mSignalSemaphores.push_back(semaphore); };
	inline void WaitOn(vk::PipelineStageFlags stage, shared_ptr<Semaphore> semaphore) { mWaitSemaphores.push_back(make_pair(stage, semaphore)); }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const string& label, const float4& color = { 1,1,1,0 });
	STRATUM_API void EndLabel();

	STRATUM_API shared_ptr<Buffer> GetBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API shared_ptr<Texture> GetTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal);
	STRATUM_API shared_ptr<DescriptorSet> GetDescriptorSet(const string& name, vk::DescriptorSetLayout layout);

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(shared_ptr<Buffer> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(shared_ptr<Texture> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(shared_ptr<DescriptorSet> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(shared_ptr<Framebuffer> resource);
	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	STRATUM_API void TrackResource(shared_ptr<RenderPass> resource);

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

	STRATUM_API void ClearAttachments(const vector<vk::ClearAttachment>& value);

	STRATUM_API void BindPipeline(shared_ptr<Pipeline> pipeline);
	
	STRATUM_API void BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> frameBuffer, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	STRATUM_API void BindDescriptorSet(shared_ptr<DescriptorSet> descriptorSet, uint32_t set);

	// Find the range for a push constant (in the current pipeline's layout) named 'name' and push it
	STRATUM_API bool PushConstant(const string& name, const byte_blob& data);
	template<typename T> inline bool PushConstantRef(const string& name, const T& value) { return PushConstant(name, byte_blob(sizeof(T), &value)); }

	inline void Dispatch(const uint2& dim) { mCommandBuffer.dispatch(dim.x, dim.y, 1); }
	inline void Dispatch(const uint3& dim) { mCommandBuffer.dispatch(dim.x, dim.y, dim.z); }
	inline void Dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	// Call Dispatch() on ceil(size / workgroupSize)
	inline void DispatchAligned(const uint3& size) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		Dispatch((size + cp->WorkgroupSize() - 1) / cp->WorkgroupSize());
	}
	inline void DispatchAligned(const uint2& size) { DispatchAligned(uint3(size, 1)); }
	inline void DispatchAligned(uint32_t x, uint32_t y = 1, uint32_t z = 1) { DispatchAligned(uint3(x, y, z)); }

	STRATUM_API void BindVertexBuffer(const ArrayBufferView& view, uint32_t index);
	STRATUM_API void BindIndexBuffer(const ArrayBufferView& view);

	STRATUM_API void DrawMesh(Mesh& mesh, uint32_t instanceCount = 1, uint32_t firstInstance = 0);
	
private:
	friend class stm::Device;

	enum class CommandBufferState { eRecording, eInFlight, eDone };

	vk::CommandBuffer mCommandBuffer;
	string mName;
	stm::Device& mDevice;
	Fence* mSignalFence;

	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	
	stm::QueueFamily* mQueueFamily;
	vk::CommandPool mCommandPool;
	CommandBufferState mState;

	vector<shared_ptr<Semaphore>> mSignalSemaphores;
	vector<pair<vk::PipelineStageFlags, shared_ptr<Semaphore>>> mWaitSemaphores;

	// Tracked resources

	vector<shared_ptr<Buffer>> mBuffers;
	vector<shared_ptr<Texture>> mTextures;
	vector<shared_ptr<DescriptorSet>> mDescriptorSets;
	vector<shared_ptr<Framebuffer>> mFramebuffers;
	vector<shared_ptr<RenderPass>> mRenderPasses;

	// Currently bound objects

	shared_ptr<Framebuffer> mCurrentFramebuffer;
	shared_ptr<RenderPass> mCurrentRenderPass;
	uint32_t mCurrentSubpassIndex = 0;
	ShaderPassIdentifier mCurrentShaderPass;
	shared_ptr<Pipeline> mBoundPipeline = nullptr;
	unordered_map<uint32_t, ArrayBufferView> mBoundVertexBuffers;
	ArrayBufferView mBoundIndexBuffer;
	vector<shared_ptr<DescriptorSet>> mBoundDescriptorSets;
	
	// assumes a CommandPool has been created for this_thread in queueFamily
	STRATUM_API CommandBuffer(const string& name, stm::Device& device, stm::QueueFamily* queueFamily, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API void Clear();

	STRATUM_API bool CheckDone();

};

class ProfilerRegion {
private:
	CommandBuffer* mCommandBuffer;
public:
	inline ProfilerRegion(const string& label) : ProfilerRegion(label, nullptr) {}
	inline ProfilerRegion(const string& label, CommandBuffer& cmd) : ProfilerRegion(label, &cmd) {}
	inline ProfilerRegion(const string& label, CommandBuffer* cmd) : mCommandBuffer(cmd) {
		if (mCommandBuffer) mCommandBuffer->BeginLabel(label);
		Profiler::BeginSample(label);
	}
	inline ~ProfilerRegion() {
		Profiler::EndSample();
		if (mCommandBuffer) mCommandBuffer->EndLabel();
	}
};

}