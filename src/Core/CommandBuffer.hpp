#pragma once

#include "DescriptorSet.hpp"
#include "Framebuffer.hpp"
#include "Pipeline.hpp"
#include "Profiler.hpp"

namespace stm {

class CommandBuffer {
public:
	Device& mDevice;
	
	// assumes a CommandPool has been created for this_thread in queueFamily
	STRATUM_API CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	inline ~CommandBuffer() {
		if (mState == CommandBufferState::eInFlight)
			fprintf_color(ConsoleColorBits::eYellow, stderr, "destroying CommandBuffer %s that is in-flight\n", mName.c_str());
		Clear();
		mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
	}
	inline vk::CommandBuffer operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Fence& CompletionFence() const { return *mCompletionFence; }
	inline Device::QueueFamily* QueueFamily() const { return mQueueFamily; }

	inline shared_ptr<RenderPass> CurrentRenderPass() const { return mCurrentRenderPass; }
	inline shared_ptr<Framebuffer> CurrentFramebuffer() const { return mCurrentFramebuffer; }
	inline uint32_t CurrentSubpassIndex() const { return mCurrentSubpassIndex; }
	inline shared_ptr<Pipeline> BoundPipeline() const { return mBoundPipeline; }

	STRATUM_API void Reset(const string& name = "Command Buffer");

	inline void SignalOnComplete(vk::PipelineStageFlags, shared_ptr<Semaphore> semaphore) { mSignalSemaphores.push_back(semaphore); };
	inline void WaitOn(vk::PipelineStageFlags stage, shared_ptr<Semaphore> semaphore) { mWaitSemaphores.push_back(make_pair(stage, semaphore)); }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const string& label, const Vector4f& color = { 1,1,1,0 });
	STRATUM_API void EndLabel();

	inline shared_ptr<Buffer> GetBuffer(const string& name, vk::DeviceSize size, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal) {
		return mBuffers.emplace_back(mDevice.GetPooledBuffer(name, size, usage, memoryProperties));
	}
	inline shared_ptr<Buffer> GetBuffer(const string& name, const byte_blob& src, vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal) {
		auto b = GetBuffer(name, src.size(), usage, memoryProperties);
		if (b->data())
			memcpy(b->data(), src.data(), src.size());
		else {
			auto tmp = GetBuffer(name+"/staging", src.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);
			mCommandBuffer.copyBuffer(**tmp, **b, { vk::BufferCopy(0, 0, src.size()) });
		}
		return b;
	}
	inline shared_ptr<Texture> GetTexture(const string& name, const vk::Extent3D& extent, vk::Format format, uint32_t mipLevels = 1, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal) {
		auto tex = mDevice.GetPooledTexture(name, extent, format, mipLevels, sampleCount, usage, memoryProperties);
		mTextures.push_back(tex);
		return tex;
	}
	inline shared_ptr<DescriptorSet> GetDescriptorSet(const string& name, vk::DescriptorSetLayout layout) {
		auto ds = mDevice.GetPooledDescriptorSet(name, layout);
		mDescriptorSets.push_back(ds);
		return ds;
	}

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

	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, { barrier }, {}, {});
	}
	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, { barrier }, {});
	}
	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, { barrier });
	}

	inline void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
		TransitionBarrier(image, subresourceRange, GuessStage(oldLayout), GuessStage(newLayout), oldLayout, newLayout);
	}
	inline void TransitionBarrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
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

	STRATUM_API void ClearAttachments(const vector<vk::ClearAttachment>& value);

	STRATUM_API void BindPipeline(shared_ptr<Pipeline> pipeline);
	
	STRATUM_API void BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> frameBuffer, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	STRATUM_API void BindDescriptorSet(shared_ptr<DescriptorSet> descriptorSet, uint32_t set);

	// Find the range for a push constant (in the current pipeline's layout) named 'name' and push it
	STRATUM_API bool PushConstant(const string& name, const byte_blob& data);
	template<typename T> inline bool PushConstantRef(const string& name, const T& value) { return PushConstant(name, byte_blob(sizeof(T), &value)); }

	inline void Dispatch(const vk::Extent2D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, 1); }
	inline void Dispatch(const vk::Extent3D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, dim.depth); }
	inline void Dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	// Call Dispatch() on ceil(size / workgroupSize)
	inline void DispatchTiled(const vk::Extent3D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		DispatchTiled(vk::Extent3D(
			(dim.width + cp->WorkgroupSize().width - 1) / cp->WorkgroupSize().width,
			(dim.height + cp->WorkgroupSize().height - 1) / cp->WorkgroupSize().height, 
			(dim.depth + cp->WorkgroupSize().depth - 1) / cp->WorkgroupSize().depth));
	}
	inline void DispatchTiled(const vk::Extent2D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		DispatchTiled(vk::Extent3D((dim.width + cp->WorkgroupSize().width - 1) / cp->WorkgroupSize().width, (dim.height + cp->WorkgroupSize().height - 1) / cp->WorkgroupSize().height, 1));
	}
	inline void DispatchTiled(uint32_t x, uint32_t y = 1, uint32_t z = 1) { return DispatchTiled(vk::Extent3D(x,y,z)); }

	inline void BindVertexBuffer(uint32_t index, const Buffer::ArrayView<>& view) {
		if (mBoundVertexBuffers[index] != view) {
			mBoundVertexBuffers[index] = view;
			mCommandBuffer.bindVertexBuffers(index, { **view }, { view.offset() });
		}
	}
	template<ranges::input_range R> requires(convertible_to<ranges::range_value_t<R>, Buffer::ArrayView<>>)
	inline void BindVertexBuffers(uint32_t index, const R& views) {
		uint32_t i = 0;
		vector<vk::Buffer> bufs(views.size());
		vector<vk::DeviceSize> offsets(views.size());
		bool bound = true;
		for (auto& v : views) {
			bufs[i] = **v;
			offsets[i] = v.offset();
			if (mBoundVertexBuffers[index + i] != v) {
				bound = false;
				mBoundVertexBuffers[index + i] = v;
			}
			i++;
		}
		if (!bound) mCommandBuffer.bindVertexBuffers(index, bufs, offsets);
	}
	template<typename T>
	inline void BindIndexBuffer(const Buffer::ArrayView<T>& view) {
		static const unordered_map<size_t, vk::IndexType> sizeMap {
			{ sizeof(uint16_t), vk::IndexType::eUint16 },
			{ sizeof(uint32_t), vk::IndexType::eUint32 },
			{ sizeof(uint8_t), vk::IndexType::eUint8EXT }
		};
		if (mBoundIndexBuffer != view) {
			mBoundIndexBuffer = view;
			mCommandBuffer.bindIndexBuffer(**view, view.offset(), sizeMap.at(view.stride()));
		}
	}

private:
	friend class Device;

	enum class CommandBufferState { eRecording, eInFlight, eDone };

	vk::CommandBuffer mCommandBuffer;
	string mName;
	unique_ptr<Fence> mCompletionFence;

	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	
	Device::QueueFamily* mQueueFamily;
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
	shared_ptr<Pipeline> mBoundPipeline = nullptr;
	unordered_map<uint32_t, Buffer::ArrayView<>> mBoundVertexBuffers;
	Buffer::ArrayView<> mBoundIndexBuffer;
	vector<shared_ptr<DescriptorSet>> mBoundDescriptorSets;
	
	STRATUM_API void Clear();
	STRATUM_API bool CheckDone();
	
public:
	size_t mPrimitiveCount;
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