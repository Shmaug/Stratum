#pragma once

#include "Fence.hpp"
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
			fprintf_color(ConsoleColorBits::eYellow, stderr, "destroying CommandBuffer %s that is in-flight\n", name().c_str());
		Clear();
		mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
	}

	inline const vk::CommandBuffer& operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Fence& completion_fence() const { return *mCompletionFence; }
	inline Device::QueueFamily* QueueFamily() const { return mQueueFamily; }
	
	inline shared_ptr<Framebuffer> bound_framebuffer() const { return mBoundFramebuffer; }
	inline uint32_t subpass_index() const { return mSubpassIndex; }
	inline shared_ptr<Pipeline> bound_pipeline() const { return mBoundPipeline; }

	STRATUM_API void Reset(const string& name = "Command Buffer");

	// cause a stage to delay execution until waitSemaphore signals
	inline void WaitOn(vk::PipelineStageFlags stage, Semaphore& waitSemaphore) { mWaitSemaphores.emplace_back(stage, forward<Semaphore&>(waitSemaphore)); }
	inline void SignalOnComplete(vk::PipelineStageFlags, shared_ptr<Semaphore> semaphore) { mSignalSemaphores.push_back(semaphore); };

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	template<derived_from<DeviceResource> T>
	inline T& HoldResource(const shared_ptr<T>& r) {
		return *static_cast<T*>(mHeldResources.emplace(static_pointer_cast<DeviceResource>(r)).first->get());
	}
	template<typename T>
	inline const Buffer::View<T>& HoldResource(const Buffer::View<T>& v) {
		mHeldResources.emplace(static_pointer_cast<DeviceResource>(v.buffer_ptr()));
		return v;
	}
	inline const Buffer::TexelView& HoldResource(const Buffer::TexelView& v) {
		mHeldResources.emplace(static_pointer_cast<DeviceResource>(v.buffer_ptr()));
		return v;
	}
	inline const Buffer::StrideView& HoldResource(const Buffer::StrideView& v) {
		mHeldResources.emplace(static_pointer_cast<DeviceResource>(v.buffer_ptr()));
		return v;
	}
	inline const Texture::View& HoldResource(const Texture::View& v) {
		mHeldResources.emplace(static_pointer_cast<DeviceResource>(v.texture_ptr()));
		return v;
	}

	// Label a region for a tool such as RenderDoc
	STRATUM_API void BeginLabel(const string& label, const Vector4f& color = { 1,1,1,0 });
	STRATUM_API void EndLabel();

	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, { barrier }, {}, {});
	}
	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, { barrier }, {});
	}
	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& barrier) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, { barrier });
	}

	template<typename T>
	inline void Barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::AccessFlags srcFlags, vk::AccessFlags dstFlags, const Buffer::View<T>& buffer) {
		Barrier(srcStage, dstStage, vk::BufferMemoryBarrier(srcFlags, dstFlags, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *buffer.buffer(), buffer.offset(), buffer.size_bytes()));
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

	template<typename...T> requires(is_same_v<T,DescriptorSet> && ...)
	inline void TransitionImageDescriptors(T&... sets) {
		(sets.FlushWrites(), ...);
		auto fn = [&](auto& descriptorSet) {
			for (auto&[idx, entry] : descriptorSet.mBoundDescriptors) {
				switch (descriptorSet.layout_at(idx >> 32).mDescriptorType) {
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eInputAttachment:
					case vk::DescriptorType::eSampledImage:
					case vk::DescriptorType::eStorageImage: {
						get<Texture::View>(entry).texture().TransitionBarrier(*this, get<vk::ImageLayout>(entry));
						break;
					}
				}
			}
		};
		(fn(sets), ...);
	}

	template<typename T, typename S>
	inline const Buffer::View<S>& CopyBuffer(const Buffer::View<T>& src, const Buffer::View<S>& dst) {
		if (src.size_bytes() != dst.size_bytes()) throw invalid_argument("src and dst must be the same size");
		mCommandBuffer.copyBuffer(*HoldResource(src.buffer_ptr()), *HoldResource(dst.buffer_ptr()), { vk::BufferCopy(src.offset(), dst.offset(), src.size_bytes()) });
	}

	template<typename T>
	inline Buffer::View<T> CopyBuffer(const Buffer::View<T>& src, vk::BufferUsageFlagBits bufferUsage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY) {
		auto dst = make_shared<Buffer>(mDevice, src.buffer().name(), src.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eTransferDst, memoryUsage);
		mCommandBuffer.copyBuffer(*HoldResource(src.buffer_ptr()), *HoldResource(dst), { vk::BufferCopy(src.offset(), 0, src.size_bytes()) });
		return dst;
	}

	inline void Dispatch(const vk::Extent2D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, 1); }
	inline void Dispatch(const vk::Extent3D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, dim.depth); }
	inline void Dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	// Call Dispatch() on ceil(size / workgroupSize)
	inline void DispatchTiled(const vk::Extent3D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->WorkgroupSize().width - 1) / cp->WorkgroupSize().width,
			(dim.height + cp->WorkgroupSize().height - 1) / cp->WorkgroupSize().height, 
			(dim.depth + cp->WorkgroupSize().depth - 1) / cp->WorkgroupSize().depth);
	}
	inline void DispatchTiled(const vk::Extent2D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch((dim.width + cp->WorkgroupSize().width - 1) / cp->WorkgroupSize().width, (dim.height + cp->WorkgroupSize().height - 1) / cp->WorkgroupSize().height, 1);
	}
	inline void DispatchTiled(uint32_t x, uint32_t y = 1, uint32_t z = 1) { return DispatchTiled(vk::Extent3D(x,y,z)); }

	STRATUM_API void BeginRenderPass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> frameBuffer, const vector<vk::ClearValue>& clearValues, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void EndRenderPass();

	inline void BindPipeline(shared_ptr<Pipeline> pipeline) {
		if (mBoundPipeline == pipeline) return;
		mCommandBuffer.bindPipeline(pipeline->BindPoint(), **pipeline);
		mBoundPipeline = pipeline;
		mBoundDescriptorSets.clear(); // TODO: do descriptorsets need to be cleared?
		mBoundVertexBuffers.clear();
		mBoundIndexBuffer = {};
		HoldResource(pipeline);
	}
	
	template<typename T>
	inline void push_constant(const string& name, const T& value) {
		auto it = mBoundPipeline->PushConstants().find(name);
		if (it == mBoundPipeline->PushConstants().end()) throw invalid_argument("push constant not found");
		const auto& range = it->second;
		if constexpr (is_same_v<T, byte_blob>) {
			if (range.size != value.size()) throw invalid_argument("argument size (" + to_string(value.size()) + ") must match push constant size (" + to_string(range.size) +")");
			mCommandBuffer.pushConstants(mBoundPipeline->layout(), range.stageFlags, range.offset, range.size, value.data());
		} else {
			static_assert(is_trivially_copyable_v<T>);
			if (range.size != sizeof(T)) throw invalid_argument("argument size (" + to_string(sizeof(T)) + ") must match push constant size (" + to_string(range.size) +")");
			mCommandBuffer.pushConstants(mBoundPipeline->layout(), range.stageFlags, range.offset, range.size, &value);
		}
	}

	template<ranges::range R> requires(is_same_v<shared_ptr<DescriptorSet>, ranges::range_value_t<R>>)
	inline void BindDescriptorSets(uint32_t index, R&& descriptorSets) {
		if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
		vector<vk::DescriptorSet> sets;
		vector<uint32_t> dynamicOffsets;
		for (const shared_ptr<DescriptorSet>& descriptorSet : descriptorSets) {
			descriptorSet->FlushWrites();
			HoldResource(descriptorSet);
			if (!mBoundFramebuffer) TransitionImageDescriptors(*descriptorSet);

			if (index + sets.size() >= mBoundDescriptorSets.size()) mBoundDescriptorSets.resize(index + sets.size() + 1);
			mBoundDescriptorSets[index + sets.size()] = descriptorSet;
			sets.push_back(**descriptorSet);
		}
		mCommandBuffer.bindDescriptorSets(mBoundPipeline->BindPoint(), mBoundPipeline->layout(), index, sets, dynamicOffsets);
	}
	inline void BindDescriptorSet(uint32_t index, shared_ptr<DescriptorSet> descriptorSet) {
		BindDescriptorSets(index, span(&descriptorSet,1));
	}

	template<typename T>
	inline void BindVertexBuffer(uint32_t index, const Buffer::View<T>& view) {
		if (mBoundVertexBuffers[index] != view) {
			mBoundVertexBuffers[index] = view;
			mCommandBuffer.bindVertexBuffers(index, { *view.buffer() }, { view.offset() });
		}
	}
	template<ranges::input_range R>
	inline void BindVertexBuffers(uint32_t index, const R& views) {
		vector<vk::Buffer> bufs(views.size());
		vector<vk::DeviceSize> offsets(views.size());
		bool needBind = false;
		uint32_t i = 0;
		for (const auto& v : views) {
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
	inline void BindIndexBuffer(const Buffer::StrideView& view) {
		if (mBoundIndexBuffer != view) {
			mBoundIndexBuffer = view;
			vk::IndexType type;
			if      (view.stride() == sizeof(uint32_t)) type = vk::IndexType::eUint32;
			else if (view.stride() == sizeof(uint16_t)) type = vk::IndexType::eUint16;
			else if (view.stride() == sizeof(uint8_t))  type = vk::IndexType::eUint8EXT;
			else throw invalid_argument("invalid stride for index buffer");
			mCommandBuffer.bindIndexBuffer(*view.buffer(), view.offset(), type);
		}
	}

	size_t mPrimitiveCount;

private:
	friend class Device;

	enum class CommandBufferState { eRecording, eInFlight, eDone };

	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = 0;
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = 0;
	
	STRATUM_API void Clear();
	inline bool CheckDone() {
		if (mState == CommandBufferState::eInFlight)
			if (mCompletionFence->status() == vk::Result::eSuccess) {
				mState = CommandBufferState::eDone;
				Clear();
				return true;
			}
		return mState == CommandBufferState::eDone;
	}
	
	vk::CommandBuffer mCommandBuffer;

	Device::QueueFamily* mQueueFamily;
	vk::CommandPool mCommandPool;
	CommandBufferState mState;
	
	unique_ptr<Fence> mCompletionFence;

	vector<shared_ptr<Semaphore>> mSignalSemaphores;
	vector<pair<vk::PipelineStageFlags, Semaphore&>> mWaitSemaphores;

	unordered_set<shared_ptr<DeviceResource>> mHeldResources;

	// Currently bound objects
	shared_ptr<Framebuffer> mBoundFramebuffer;
	uint32_t mSubpassIndex = 0;
	shared_ptr<Pipeline> mBoundPipeline = nullptr;
	unordered_map<uint32_t, Buffer::View<byte>> mBoundVertexBuffers;
	Buffer::StrideView mBoundIndexBuffer;
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