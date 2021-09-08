#pragma once

#include "Fence.hpp"
#include "Framebuffer.hpp"
#include "Pipeline.hpp"
#include <Common/Profiler.hpp>

namespace stm {

class CommandBuffer : public DeviceResource {
public:	
	// assumes a CommandPool has been created for this_thread in queueFamily
	STRATUM_API CommandBuffer(Device& device, const string& name, Device::QueueFamily* queueFamily, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API ~CommandBuffer();

	inline vk::CommandBuffer& operator*() { return mCommandBuffer; }
	inline vk::CommandBuffer* operator->() { return &mCommandBuffer; }
	inline const vk::CommandBuffer& operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Fence& completion_fence() const { return *mCompletionFence; }
	inline Device::QueueFamily* queue_family() const { return mQueueFamily; }
	
	inline const shared_ptr<Framebuffer>& bound_framebuffer() const { return mBoundFramebuffer; }
	inline uint32_t subpass_index() const { return mSubpassIndex; }
	inline const shared_ptr<Pipeline>& bound_pipeline() const { return mBoundPipeline; }
	inline const shared_ptr<DescriptorSet>& bound_descriptor_set(uint32_t index) const { return mBoundDescriptorSets[index]; }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void begin_label(const string& label, const Vector4f& color = { 1,1,1,0 });
	STRATUM_API void end_label();

	STRATUM_API void reset(const string& name = "Command Buffer");

	// Add a resource to the device's resource pool after this commandbuffer finishes executing
	template<derived_from<DeviceResource> T>
	inline T& hold_resource(const shared_ptr<T>& r) {
		r->mTracking.emplace(this);
		return *static_cast<T*>(mHeldResources.emplace( static_pointer_cast<DeviceResource>(r) ).first->get());
	}
	template<typename T>
	inline const Buffer::View<T>& hold_resource(const Buffer::View<T>& v) {
		hold_resource(v.buffer());
		return v;
	}
	inline const Buffer::TexelView& hold_resource(const Buffer::TexelView& v) {
		hold_resource(v.buffer());
		return v;
	}
	inline const Buffer::StrideView& hold_resource(const Buffer::StrideView& v) {
		hold_resource(v.buffer());
		return v;
	}
	inline const Texture::View& hold_resource(const Texture::View& v) {
		hold_resource(v.texture());
		return v;
	}

	inline void memory_barrier(vk::PipelineStageFlags srcStage, vk::AccessFlags srcAccessMask, vk::PipelineStageFlags dstStage, vk::AccessFlags dstAccessMask) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, { vk::MemoryBarrier(srcAccessMask, dstAccessMask) }, {}, {});
	}
	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::BufferMemoryBarrier& b) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, { b }, {});
	}
	inline void barrier(vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage, const vk::ImageMemoryBarrier& b) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, { b });
	}
	template<typename T = byte>
	inline void barrier(vk::PipelineStageFlags srcStage, vk::AccessFlags srcAccessMask, vk::PipelineStageFlags dstStage, vk::AccessFlags dstAccessMask, const Buffer::View<T>& buffer) {
		barrier(srcStage, dstStage, vk::BufferMemoryBarrier(srcAccessMask, dstAccessMask, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, **buffer.buffer(), buffer.offset(), buffer.size_bytes()));
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

	template<typename T = byte, typename S = T>
	inline const Buffer::View<S>& copy_buffer(const Buffer::View<T>& src, const Buffer::View<S>& dst) {
		if (src.size_bytes() != dst.size_bytes()) throw invalid_argument("src and dst must be the same size");
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer()), *hold_resource(dst.buffer()), { vk::BufferCopy(src.offset(), dst.offset(), src.size_bytes()) });
		return dst;
	}
	template<typename T = byte>
	inline Buffer::View<T> copy_buffer(const Buffer::View<T>& src, vk::BufferUsageFlagBits bufferUsage, VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY) {
		shared_ptr<Buffer> dst = make_shared<Buffer>(mDevice, src.buffer()->name(), src.size_bytes(), bufferUsage|vk::BufferUsageFlagBits::eTransferDst, memoryUsage);
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer()), *hold_resource(dst), { vk::BufferCopy(src.offset(), 0, src.size_bytes()) });
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
			copies[i] = vk::ImageCopy(src.subresource(i), {}, dst.subresource(i), {}, src.texture()->extent());
		} else
			copies.emplace_back(src.subresource(level), vk::Offset3D(), dst.subresource(level), vk::Offset3D(), src.texture()->extent());
		src.texture()->transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		dst.texture()->transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.copyImage(*hold_resource(src.texture()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.texture()), vk::ImageLayout::eTransferDstOptimal, copies);
		return dst;
	}

	template<typename T = byte>
	inline const Texture::View& copy_buffer_to_image(const Buffer::View<T>& src, const Texture::View& dst, uint32_t level = 0) {
		dst.texture()->transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.copyBufferToImage(*hold_resource(src.buffer()), *hold_resource(dst.texture()), vk::ImageLayout::eTransferDstOptimal, {
			vk::BufferImageCopy(src.offset(), 0, 0, dst.subresource(level), {}, dst.texture()->extent()) });
		return dst;
	}

	inline void dispatch(const vk::Extent2D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, 1); }
	inline void dispatch(const vk::Extent3D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, dim.depth); }
	inline void dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	
	// dispatch on ceil(size / workgroupSize)
	inline void dispatch_align(const vk::Extent2D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->workgroup_size()[0] - 1) / cp->workgroup_size()[0],
			(dim.height + cp->workgroup_size()[1] - 1) / cp->workgroup_size()[1],
			1);
	}
	inline void dispatch_align(const vk::Extent3D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->workgroup_size()[0] - 1) / cp->workgroup_size()[0],
			(dim.height + cp->workgroup_size()[1] - 1) / cp->workgroup_size()[1], 
			(dim.depth + cp->workgroup_size()[2] - 1) / cp->workgroup_size()[2]);
	}
	inline void dispatch_align(uint32_t x, uint32_t y = 1, uint32_t z = 1) { return dispatch_align(vk::Extent3D(x,y,z)); }

	STRATUM_API void begin_render_pass(const shared_ptr<RenderPass>& renderPass, const shared_ptr<Framebuffer>& framebuffer, const vk::Rect2D& renderArea, const vk::ArrayProxyNoTemporaries<const vk::ClearValue>& clearValues, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void next_subpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void end_render_pass();

	inline bool bind_pipeline(shared_ptr<Pipeline> pipeline) {
		if (mBoundPipeline == pipeline) return false;
		mCommandBuffer.bindPipeline(pipeline->bind_point(), **pipeline);
		mBoundPipeline = pipeline;
		hold_resource(pipeline);
		return true;
	}
	
	template<typename T>
	inline void push_constant(const string& name, const T& value) {
		auto it = mBoundPipeline->push_constants().find(name);
		if (it == mBoundPipeline->push_constants().end()) throw invalid_argument("push constant not found");
		const auto& range = it->second;
		if constexpr (ranges::contiguous_range<T>) {
			if (range.size != ranges::size(value)*sizeof(ranges::range_value_t<T>)) throw invalid_argument("argument size must match push constant size (" + to_string(range.size) +")");
			mCommandBuffer.pushConstants(mBoundPipeline->layout(), range.stageFlags, range.offset, range.size, ranges::data(value));
		} else {
			if (range.size != sizeof(T)) throw invalid_argument("argument size must match push constant size (" + to_string(range.size) +")");
			mCommandBuffer.pushConstants(mBoundPipeline->layout(), range.stageFlags, range.offset, sizeof(T), &value);
		}
	}

	template<typename...T> requires(same_as<T,DescriptorSet> && ...)
	inline void transition_images(T&... sets) {
		auto fn = [&](auto& descriptorSet) {
			for (auto&[idx, entry] : descriptorSet.mDescriptors)
				switch (descriptorSet.layout_at(idx >> 32).mDescriptorType) {
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eInputAttachment:
					case vk::DescriptorType::eSampledImage:
					case vk::DescriptorType::eStorageImage:
						get<Texture::View>(entry).texture()->transition_barrier(*this, get<vk::ImageLayout>(entry));
						break;
				}
		};
		(sets.flush_writes(), ...);
		(fn(sets), ...);
	}

	STRATUM_API void bind_descriptor_set(uint32_t index, const shared_ptr<DescriptorSet>& descriptorSet, const vk::ArrayProxy<const uint32_t>& dynamicOffsets);
	inline void bind_descriptor_set(uint32_t index, const shared_ptr<DescriptorSet>& descriptorSet) {
		if (index < mBoundDescriptorSets.size() && mBoundDescriptorSets[index] == descriptorSet) return;
		bind_descriptor_set(index, descriptorSet, {});
	}

	template<typename T=byte>
	inline void bind_vertex_buffer(uint32_t index, const Buffer::View<T>& view) {
		auto& b = mBoundVertexBuffers[index];
		if (b != view) {
			b = view;
			mCommandBuffer.bindVertexBuffers(index, **view.buffer(), view.offset());
			hold_resource(view);
		}
	}
	template<ranges::input_range R>
	inline void bind_vertex_buffers(uint32_t index, const R& views) {
		vector<vk::Buffer> bufs(views.size());
		vector<vk::DeviceSize> offsets(views.size());
		bool needBind = false;
		uint32_t i = 0;
		for (const auto& v : views) {
			bufs[i] = **v.buffer();
			offsets[i] = v.offset();
			if (mBoundVertexBuffers[index + i] != v) {
				needBind = true;
				mBoundVertexBuffers[index + i] = v;
				hold_resource(v);
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
			mCommandBuffer.bindIndexBuffer(**view.buffer(), view.offset(), type);
			hold_resource(view);
		}
	}

	inline Buffer::View<byte> current_vertex_buffer(uint32_t index) {
		auto it = mBoundVertexBuffers.find(index);
		if (it == mBoundVertexBuffers.end()) return {};
		else return it->second;
	}
	inline const Buffer::StrideView& current_index_buffer() { return mBoundIndexBuffer; }

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