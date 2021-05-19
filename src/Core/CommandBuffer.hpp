#pragma once

#include "Fence.hpp"
#include "Framebuffer.hpp"
#include "Pipeline.hpp"
#include "../Common/Profiler.hpp"

namespace stm {

class CommandBuffer : public DeviceResource {
public:	
	// assumes a CommandPool has been created for this_thread in queueFamily
	STRATUM_API CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	inline ~CommandBuffer() {
		if (mState == CommandBufferState::eInFlight)
			fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Destroying CommandBuffer [%s] that is in-flight!\n", name().c_str());
		clear();
		mDevice->freeCommandBuffers(mCommandPool, { mCommandBuffer });
	}

	inline vk::CommandBuffer& operator*() { return mCommandBuffer; }
	inline vk::CommandBuffer* operator->() { return &mCommandBuffer; }
	inline const vk::CommandBuffer& operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Fence& completion_fence() const { return *mCompletionFence; }
	inline Device::QueueFamily* queue_family() const { return mQueueFamily; }
	
	inline shared_ptr<Framebuffer> bound_framebuffer() const { return mBoundFramebuffer; }
	inline uint32_t subpass_index() const { return mSubpassIndex; }
	inline shared_ptr<Pipeline> bound_pipeline() const { return mBoundPipeline; }
	inline shared_ptr<DescriptorSet> bound_descriptor_set(uint32_t index) const { return mBoundDescriptorSets[index]; }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void begin_label(const string& label, const Vector4f& color = { 1,1,1,0 });
	STRATUM_API void end_label();

	STRATUM_API void reset(const string& name = "Command Buffer");

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	template<derived_from<DeviceResource> T>
	inline T& hold_resource(const shared_ptr<T>& r) {
		return *static_cast<T*>(mHeldResources.emplace( static_pointer_cast<DeviceResource>(r) ).first->get());
	}
	template<typename T>
	inline const Buffer::View<T>& hold_resource(const Buffer::View<T>& v) {
		hold_resource(v.buffer_ptr());
		return v;
	}
	inline const Buffer::TexelView& hold_resource(const Buffer::TexelView& v) {
		hold_resource(v.buffer_ptr());
		return v;
	}
	inline const Buffer::StrideView& hold_resource(const Buffer::StrideView& v) {
		hold_resource(v.buffer_ptr());
		return v;
	}
	inline const Texture::View& hold_resource(const Texture::View& v) {
		hold_resource(v.texture_ptr());
		return v;
	}

	template<derived_from<DeviceResource> T, typename...Args> requires(constructible_from<T,Args...>)
	inline auto& hold_resource(Args&&...args) {
		return hold_resource(make_shared<T>(forward<Args>(args)...));
	}

	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::MemoryBarrier& b) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, { b }, {}, {});
	}
	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& b) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, { b }, {});
	}
	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& b) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, { b });
	}
	template<typename T>
	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::AccessFlags srcFlags, vk::AccessFlags dstFlags, const Buffer::View<T>& buffer) {
		barrier(srcStage, dstStage, vk::BufferMemoryBarrier(srcFlags, dstFlags, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *buffer.buffer(), buffer.offset(), buffer.size_bytes()));
	}

	inline void transition_barrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
		transition_barrier(image, subresourceRange, guess_stage(oldLayout), guess_stage(newLayout), oldLayout, newLayout);
	}
	inline void transition_barrier(vk::Image image, const vk::ImageSubresourceRange& subresourceRange, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
		if (oldLayout == newLayout) return;
		vk::ImageMemoryBarrier b = {};
		b.oldLayout = oldLayout;
		b.newLayout = newLayout;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = subresourceRange;
		b.srcAccessMask = guess_access_flags(oldLayout);
		b.dstAccessMask = guess_access_flags(newLayout);
		barrier(srcStage, dstStage, b);
	}

	template<typename...T> requires(is_same_v<T,DescriptorSet> && ...)
	inline void transition_images(T&... sets) {
		auto fn = [&](auto& descriptorSet) {
			for (auto&[idx, entry] : descriptorSet.mDescriptors)
				switch (descriptorSet.layout_at(idx >> 32).mDescriptorType) {
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eInputAttachment:
					case vk::DescriptorType::eSampledImage:
					case vk::DescriptorType::eStorageImage:
						get<Texture::View>(entry).texture().transition_barrier(*this, get<vk::ImageLayout>(entry));
						break;
				}
		};
		(sets.flush_writes(), ...);
		(fn(sets), ...);
	}

	template<typename T = byte, typename S = T>
	inline const Buffer::View<S>& copy_buffer(const Buffer::View<T>& src, const Buffer::View<S>& dst) {
		if (src.size_bytes() != dst.size_bytes()) throw invalid_argument("src and dst must be the same size");
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer_ptr()), *hold_resource(dst.buffer_ptr()), { vk::BufferCopy(src.offset(), dst.offset(), src.size_bytes()) });
		return dst;
	}

	template<typename T = byte>
	inline Buffer::View<T> copy_buffer(const Buffer::View<T>& src, vk::BufferUsageFlagBits bufferUsage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY) {
		shared_ptr<Buffer> dst = make_shared<Buffer>(mDevice, src.buffer().name(), src.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eTransferDst, memoryUsage);
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer_ptr()), *hold_resource(dst), { vk::BufferCopy(src.offset(), 0, src.size_bytes()) });
		return Buffer::View<T>(dst);
	}
	
	template<typename T = byte>
	inline Buffer::View<T> copy_buffer(const buffer_vector<T>& src, vk::BufferUsageFlagBits bufferUsage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY) {
		shared_ptr<Buffer> dst = make_shared<Buffer>(mDevice, src.buffer()->name(), src.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eTransferDst, memoryUsage);
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer()), *hold_resource(dst), { vk::BufferCopy(0, 0, src.size_bytes()) });
		return Buffer::View<T>(dst);
	}

	inline const Texture::View& copy_image(const Texture::View& src, const Texture::View& dst, uint32_t level = 0) {
		vector<vk::ImageCopy> copies;
		if (level == 0) {
			copies.resize(src.subresource_range().levelCount);
			for (uint32_t i = 0; i < copies.size(); i++)
			copies[i] = vk::ImageCopy(src.subresource(i), {}, dst.subresource(i), {}, src.texture().extent());
		} else
			copies.emplace_back(src.subresource(level), vk::Offset3D(), dst.subresource(level), vk::Offset3D(), src.texture().extent());
		src.texture().transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		dst.texture().transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.copyImage(*hold_resource(src.texture_ptr()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.texture_ptr()), vk::ImageLayout::eTransferDstOptimal, copies);
		return dst;
	}

	inline void dispatch(const vk::Extent2D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, 1); }
	inline void dispatch(const vk::Extent3D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, dim.depth); }
	inline void dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	
	// dispatch on ceil(size / workgroupSize)
	inline void dispatch_align(const vk::Extent3D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->workgroup_size().width - 1) / cp->workgroup_size().width,
			(dim.height + cp->workgroup_size().height - 1) / cp->workgroup_size().height, 
			(dim.depth + cp->workgroup_size().depth - 1) / cp->workgroup_size().depth);
	}
	inline void dispatch_align(const vk::Extent2D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch((dim.width + cp->workgroup_size().width - 1) / cp->workgroup_size().width, (dim.height + cp->workgroup_size().height - 1) / cp->workgroup_size().height, 1);
	}
	inline void dispatch_align(uint32_t x, uint32_t y = 1, uint32_t z = 1) { return dispatch_align(vk::Extent3D(x,y,z)); }

	STRATUM_API void begin_render_pass(shared_ptr<RenderPass> renderPass, shared_ptr<Framebuffer> frameBuffer, const vk::ArrayProxy<const vk::ClearValue>& clearValues = {}, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void next_subpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void end_render_pass();

	inline bool bind_pipeline(shared_ptr<Pipeline> pipeline) {
		if (mBoundPipeline == pipeline) return false;
		mCommandBuffer.bindPipeline(pipeline->bind_point(), **pipeline);
		mBoundPipeline = pipeline;
		mBoundDescriptorSets.clear(); // TODO: do descriptorsets need to be cleared?
		mBoundVertexBuffers.clear();
		mBoundIndexBuffer = {};
		hold_resource(pipeline);
		return true;
	}
	
	template<typename T>
	inline void push_constant(const string& name, const T& value) {
		auto it = mBoundPipeline->push_constants().find(name);
		if (it == mBoundPipeline->push_constants().end()) throw invalid_argument("push constant not found");
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
	
	inline void bind_descriptor_set(uint32_t index, shared_ptr<DescriptorSet> descriptorSet, const vk::ArrayProxy<const uint32_t>& dynamicOffsets = {}) {
		if (!mBoundPipeline) throw logic_error("attempt to bind descriptor sets without a pipeline bound\n");
		hold_resource(descriptorSet);
		
		vector<vk::DescriptorSet> sets;
		vector<uint32_t> offsets;
		descriptorSet->flush_writes();
		if (!mBoundFramebuffer) transition_images(*descriptorSet);

		if (index + sets.size() >= mBoundDescriptorSets.size()) mBoundDescriptorSets.resize(index + sets.size() + 1);
		mBoundDescriptorSets[index + sets.size()] = descriptorSet;
		sets.emplace_back(**descriptorSet);
		mCommandBuffer.bindDescriptorSets(mBoundPipeline->bind_point(), mBoundPipeline->layout(), index, **descriptorSet, dynamicOffsets);
	}

	template<typename T>
	inline void bind_vertex_buffer(uint32_t index, const Buffer::View<T>& view) {
		if (mBoundVertexBuffers[index] != view) {
			mBoundVertexBuffers[index] = view;
			mCommandBuffer.bindVertexBuffers(index, { *view.buffer() }, { view.offset() });
		}
	}
	template<ranges::input_range R>
	inline void bind_vertex_buffers(uint32_t index, const R& views) {
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
	inline void bind_index_buffer(const Buffer::StrideView& view) {
		if (mBoundIndexBuffer != view) {
			mBoundIndexBuffer = view;
			vk::IndexType type;
			if      (view.stride() == sizeof(uint32_t)) type = vk::IndexTypeValue<uint32_t>::value;
			else if (view.stride() == sizeof(uint16_t)) type = vk::IndexTypeValue<uint16_t>::value;
			else if (view.stride() == sizeof(uint8_t))  type = vk::IndexTypeValue<uint8_t>::value;
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
	
	STRATUM_API void clear();
	inline bool clear_if_done() {
		if (mState == CommandBufferState::eInFlight)
			if (mCompletionFence->status() == vk::Result::eSuccess) {
				mState = CommandBufferState::eDone;
				clear();
				return true;
			}
		return mState == CommandBufferState::eDone;
	}
	
	vk::CommandBuffer mCommandBuffer;

	Device::QueueFamily* mQueueFamily;
	vk::CommandPool mCommandPool;
	CommandBufferState mState;
	
	unique_ptr<Fence> mCompletionFence;

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
		Profiler::begin_sample(label);
		if (mCommandBuffer) mCommandBuffer->begin_label(label);
	}
	inline ~ProfilerRegion() {
		if (mCommandBuffer) mCommandBuffer->end_label();
		Profiler::end_sample();
	}
};

}