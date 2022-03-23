#pragma once

#include <Common/Profiler.hpp>

#include "Fence.hpp"
#include "RenderPass.hpp"
#include "Pipeline.hpp"

namespace stm {

class CommandBuffer : public DeviceResource {
public:	
	// assumes a CommandPool has been created for this_thread in queueFamily
	STRATUM_API CommandBuffer(Device::QueueFamily& queueFamily, const string& name, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);
	STRATUM_API ~CommandBuffer();

	inline vk::CommandBuffer& operator*() { return mCommandBuffer; }
	inline vk::CommandBuffer* operator->() { return &mCommandBuffer; }
	inline const vk::CommandBuffer& operator*() const { return mCommandBuffer; }
	inline const vk::CommandBuffer* operator->() const { return &mCommandBuffer; }

	inline Fence& completion_fence() const { return *mCompletionFence; }
	inline Device::QueueFamily& queue_family() const { return mQueueFamily; }
	
	inline const shared_ptr<Framebuffer>& bound_framebuffer() const { return mBoundFramebuffer; }
	inline uint32_t subpass_index() const { return mSubpassIndex; }
	inline const shared_ptr<Pipeline>& bound_pipeline() const { return mBoundPipeline; }
	inline const shared_ptr<DescriptorSet>& bound_descriptor_set(uint32_t index) const { return mBoundDescriptorSets[index]; }

	// Label a region for a tool such as RenderDoc
	STRATUM_API void begin_label(const string& label, const Array4f& color = { 1,1,1,0 });
	STRATUM_API void end_label();

	STRATUM_API void reset(const string& name = "Command Buffer");

	inline bool clear_if_done() {
		if (mState == CommandBufferState::eInFlight)
			if (mCompletionFence->status() == vk::Result::eSuccess) {
				mState = CommandBufferState::eDone;
				clear();
				return true;
			}
		return mState == CommandBufferState::eDone;
	}

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
	inline const Image::View& hold_resource(const Image::View& v) {
		hold_resource(v.image());
		return v;
	}

	inline void barrier(const vk::ArrayProxy<const vk::MemoryBarrier>& b, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, b, {}, {});
	}
	inline void barrier(const vk::ArrayProxy<const vk::BufferMemoryBarrier>& b, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, b, {});
	}
	inline void barrier(const vk::ArrayProxy<const vk::ImageMemoryBarrier>& b, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {
		mCommandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, b);
	}
	template<typename T = byte>
	inline void barrier(const vk::ArrayProxy<const Buffer::View<T>>& buffers, vk::PipelineStageFlags srcStage, vk::AccessFlags srcAccessMask, vk::PipelineStageFlags dstStage, vk::AccessFlags dstAccessMask) {
		vector<vk::BufferMemoryBarrier> barriers(buffers.size());
		ranges::transform(buffers, barriers.begin(), [=](const Buffer::View<T>& buffer) {
			return vk::BufferMemoryBarrier(srcAccessMask, dstAccessMask, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, **buffer.buffer(), buffer.offset(), buffer.size_bytes());
		});
		barrier(barriers, srcStage, dstStage);
	}

	template<typename T = byte, typename S = T>
	inline const Buffer::View<S>& copy_buffer(const Buffer::View<T>& src, const Buffer::View<S>& dst) {
		if (src.size_bytes() > dst.size_bytes()) throw invalid_argument("src size must be less than or equal to dst size");
		mCommandBuffer.copyBuffer(*hold_resource(src.buffer()), *hold_resource(dst.buffer()), { vk::BufferCopy(src.offset(), dst.offset(), src.size_bytes()) });
		return dst;
	}

	template<typename T = byte>
	inline const Image::View& copy_buffer_to_image(const Buffer::View<T>& src, const Image::View& dst) {
		vector<vk::BufferImageCopy> copies(dst.subresource_range().levelCount);
		for (uint32_t i = 0; i < dst.subresource_range().levelCount; i++)
			copies[i] = vk::BufferImageCopy(src.offset(), 0, 0, dst.subresource(i), {}, dst.extent());
		dst.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.copyBufferToImage(*hold_resource(src.buffer()), *hold_resource(dst.image()), vk::ImageLayout::eTransferDstOptimal, copies);
		return dst;
	}

	template<typename T = byte>
	inline const Buffer::View<T>& copy_image_to_buffer(const Image::View& src, const Buffer::View<T>& dst) {
		vector<vk::BufferImageCopy> copies(src.subresource_range().levelCount);
		for (uint32_t i = 0; i < src.subresource_range().levelCount; i++)
			copies[i] = vk::BufferImageCopy(dst.offset(), 0, 0, src.subresource(i), {}, src.extent());
		src.transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		mCommandBuffer.copyImageToBuffer(*hold_resource(src.image()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.buffer()), copies);
		return dst;
	}

	inline const Image::View& clear_color_image(const Image::View& img, const vk::ClearColorValue& clear) {
		img.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.clearColorImage(*hold_resource(img.image()), vk::ImageLayout::eTransferDstOptimal, clear, img.subresource_range());
		return img;
	}
	inline const Image::View& clear_color_image(const Image::View& img, const vk::ClearDepthStencilValue& clear) {
		img.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		mCommandBuffer.clearDepthStencilImage(*hold_resource(img.image()), vk::ImageLayout::eTransferDstOptimal, clear, img.subresource_range());
		return img;
	}
	inline const Image::View& blit_image(const Image::View& src, const Image::View& dst, vk::Filter filter = vk::Filter::eLinear) {
		src.transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		dst.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		vector<vk::ImageBlit> blits(src.subresource_range().levelCount);
		for (uint32_t i = 0; i < blits.size(); i++) {
			array<vk::Offset3D,2> srcOffset;
			srcOffset[1].x = src.extent().width;
			srcOffset[1].y = src.extent().height;
			srcOffset[1].z = src.extent().depth;
			array<vk::Offset3D,2> dstOffset;
			dstOffset[1].x = dst.extent().width;
			dstOffset[1].y = dst.extent().height;
			dstOffset[1].z = dst.extent().depth;
			blits[i] = vk::ImageBlit(src.subresource(i), srcOffset, src.subresource(i), dstOffset);
		}
		mCommandBuffer.blitImage(*hold_resource(src.image()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.image()), vk::ImageLayout::eTransferDstOptimal, blits, filter);
		return dst;
	}
	inline const Image::View& copy_image(const Image::View& src, const Image::View& dst) {
		src.transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		dst.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		vector<vk::ImageCopy> copies(src.subresource_range().levelCount);
		for (uint32_t i = 0; i < copies.size(); i++)
			copies[i] = vk::ImageCopy(src.subresource(i), vk::Offset3D{}, src.subresource(i), vk::Offset3D{}, src.extent());
		mCommandBuffer.copyImage(*hold_resource(src.image()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.image()), vk::ImageLayout::eTransferDstOptimal, copies);
		return dst;
	}
	inline const Image::View& resolve_image(const Image::View& src, const Image::View& dst) {
		src.transition_barrier(*this, vk::ImageLayout::eTransferSrcOptimal);
		dst.transition_barrier(*this, vk::ImageLayout::eTransferDstOptimal);
		vector<vk::ImageResolve> resolves(src.subresource_range().levelCount);
		for (uint32_t i = 0; i < resolves.size(); i++)
			resolves[i] = vk::ImageResolve(src.subresource(i), vk::Offset3D{}, src.subresource(i), vk::Offset3D{}, src.extent());
		mCommandBuffer.resolveImage(*hold_resource(src.image()), vk::ImageLayout::eTransferSrcOptimal, *hold_resource(dst.image()), vk::ImageLayout::eTransferDstOptimal, resolves);
		return dst;
	}

	inline void dispatch(const vk::Extent2D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, 1); }
	inline void dispatch(const vk::Extent3D& dim) { mCommandBuffer.dispatch(dim.width, dim.height, dim.depth); }
	inline void dispatch(uint32_t x, uint32_t y=1, uint32_t z=1) { mCommandBuffer.dispatch(x, y, z); }
	
	// dispatch on ceil(size / workgroupSize)
	inline void dispatch_over(const vk::Extent2D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->workgroup_size()[0] - 1) / cp->workgroup_size()[0],
			(dim.height + cp->workgroup_size()[1] - 1) / cp->workgroup_size()[1],
			1);
	}
	inline void dispatch_over(const vk::Extent3D& dim) {
		auto cp = dynamic_pointer_cast<ComputePipeline>(mBoundPipeline);
		mCommandBuffer.dispatch(
			(dim.width + cp->workgroup_size()[0] - 1) / cp->workgroup_size()[0],
			(dim.height + cp->workgroup_size()[1] - 1) / cp->workgroup_size()[1], 
			(dim.depth + cp->workgroup_size()[2] - 1) / cp->workgroup_size()[2]);
	}
	inline void dispatch_over(uint32_t x, uint32_t y = 1, uint32_t z = 1) { return dispatch_over(vk::Extent3D(x,y,z)); }

	STRATUM_API void begin_render_pass(const shared_ptr<RenderPass>& renderPass, const shared_ptr<Framebuffer>& framebuffer, const vk::Rect2D& renderArea, const vector<vk::ClearValue>& clearValues, vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void next_subpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	STRATUM_API void end_render_pass();

	inline bool bind_pipeline(const shared_ptr<Pipeline>& pipeline) {
		if (mBoundPipeline == pipeline) return false;
		mCommandBuffer.bindPipeline(pipeline->bind_point(), **pipeline);
		mBoundPipeline = pipeline;
		hold_resource(pipeline);
		mBoundDescriptorSets.clear();
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
	
	STRATUM_API void clear();
	
	vk::CommandBuffer mCommandBuffer;

	Device::QueueFamily& mQueueFamily;
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

inline bool DeviceResource::in_use() {
	while (!mTracking.empty()) {
		if (!(*mTracking.begin())->clear_if_done())
			return true;
	}
	return !mTracking.empty();
}

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