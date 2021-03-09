#pragma once

#include "DescriptorSet.hpp"
#include "Framebuffer.hpp"
#include "Pipeline.hpp"
#include "Profiler.hpp"

namespace stm {

class CommandBuffer : public DeviceResource {
public:	
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

	inline void WaitOn(vk::PipelineStageFlags stage, Semaphore& semaphore) { mWaitSemaphores.emplace_back(stage, forward<Semaphore&>(semaphore)); }
	inline void SignalOnComplete(vk::PipelineStageFlags, shared_ptr<Semaphore> semaphore) { mSignalSemaphores.push_back(semaphore); };

	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const string& label, const Vector4f& color = { 1,1,1,0 });
	STRATUM_API void EndLabel();

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	template<typename T> requires(derived_from<T, DeviceResource>)
	inline T& HoldResource(shared_ptr<T> resource) {
		return *static_cast<T*>(mHeldResources.emplace(resource).first->get());
	}

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

	STRATUM_API void BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> frameBuffer, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	// Find the range for a push constant (in the current pipeline's layout) named 'name' and push it
	STRATUM_API bool PushConstant(const string& name, const byte_blob& data);
	template<typename T> inline bool PushConstantRef(const string& name, const T& value) { return PushConstant(name, byte_blob(sizeof(T), &value)); }

	STRATUM_API void BindPipeline(shared_ptr<Pipeline> pipeline);
	
	STRATUM_API void BindDescriptorSet(shared_ptr<DescriptorSet> descriptorSet, uint32_t set);

	inline void BindVertexBuffer(uint32_t index, const Buffer::ArrayView<>& view) {
		if (mBoundVertexBuffers[index] != view) {
			mBoundVertexBuffers[index] = view;
			mCommandBuffer.bindVertexBuffers(index, { *view.buffer() }, { view.offset() });
		}
	}
	template<ranges::input_range R> requires(convertible_to<ranges::range_value_t<R>, Buffer::ArrayView<>>)
	inline void BindVertexBuffers(uint32_t index, const R& views) {
		vector<vk::Buffer> bufs(views.size());
		vector<vk::DeviceSize> offsets(views.size());
		bool needBind = false;
		uint32_t i = 0;
		for (const Buffer::ArrayView<>& v : views) {
			bufs[i] = *v.buffer();
			offsets[i] = v.offset();
			if (mBoundVertexBuffers[index + i] != v) {
				needBind = true;
				mBoundVertexBuffers[index + i] = v;
			}
			i++;
		}
		if (needBind) mCommandBuffer.bindVertexBuffers(index, bufs, offsets);
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
			mCommandBuffer.bindIndexBuffer(*view.buffer(), view.offset(), sizeMap.at(view.stride()));
		}
	}

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

	size_t mPrimitiveCount;

private:
	friend class Device;

	enum class CommandBufferState { eRecording, eInFlight, eDone };

	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	
	STRATUM_API void Clear();
	STRATUM_API bool CheckDone();

	vk::CommandBuffer mCommandBuffer;

	Device::QueueFamily* mQueueFamily;
	vk::CommandPool mCommandPool;
	CommandBufferState mState;
	
	unique_ptr<Fence> mCompletionFence;

	vector<shared_ptr<Semaphore>> mSignalSemaphores;
	vector<pair<vk::PipelineStageFlags, Semaphore&>> mWaitSemaphores;

	unordered_set<shared_ptr<DeviceResource>> mHeldResources;

	// Currently bound objects
	shared_ptr<Framebuffer> mCurrentFramebuffer;
	shared_ptr<RenderPass> mCurrentRenderPass;
	uint32_t mCurrentSubpassIndex = 0;
	shared_ptr<Pipeline> mBoundPipeline = nullptr;
	unordered_map<uint32_t, Buffer::ArrayView<>> mBoundVertexBuffers;
	Buffer::ArrayView<> mBoundIndexBuffer;
	vector<shared_ptr<DescriptorSet>> mBoundDescriptorSets;
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