#pragma once

#include "Types.hpp"

namespace stm {

template<typename T> struct tuplefier {
	inline tuplefier() {
		static_assert(0, __FUNCTION__": is not specialized");
	};
};

template<typename _Tuple, size_t _Index> requires(is_specialization_v<_Tuple, tuple>)
constexpr bool is_tuple_of_refs_v = is_reference_v<typename tuple_element<_Index, _Tuple>::type> && is_tuple_of_refs_v<_Tuple, _Index - 1>;
template<typename _Tuple> requires(is_specialization_v<_Tuple, tuple>)
constexpr bool is_tuple_of_refs_v<_Tuple, 0> = is_reference_v<typename tuple_element<0, _Tuple>::type>;

template<typename _Tuple>
concept is_tuple_of_refs = is_tuple_of_refs_v<_Tuple, tuple_size_v<_Tuple> - 1>;

template<typename T> using tuplefied_t = decltype(declval<tuplefier<T>>()( forward<T>(declval<T>()) ));
template<typename T> concept tuplefiable = is_tuple_of_refs<tuplefied_t<T>>;

template<> struct tuplefier<vk::AttachmentDescription> {
	inline auto operator()(vk::AttachmentDescription&& a) const {
		return forward_as_tuple(
			a.flags,
			a.format,
			a.samples,
			a.loadOp,
			a.storeOp,
			a.stencilLoadOp,
			a.stencilStoreOp,
			a.initialLayout,
			a.finalLayout);
	}
};
template<> struct tuplefier<vk::ComponentMapping> {
	inline auto operator()(vk::ComponentMapping&& v) const {
		return forward_as_tuple(v.r, v.g, v.b, v.a);
	}
};
template<> struct tuplefier<vk::Extent2D> {
	inline auto operator()(vk::Extent2D&& v) const {
		return forward_as_tuple(v.width, v.height);
	}
};
template<> struct tuplefier<vk::Extent3D> {
	inline auto operator()(vk::Extent3D&& v) const {
		return forward_as_tuple(v.width, v.height, v.depth);
	}
};
template<> struct tuplefier<vk::Offset2D> {
	inline auto operator()(vk::Offset2D&& v) const {
		return forward_as_tuple(v.x, v.y);
	}
};
template<> struct tuplefier<vk::Offset3D> {
	inline auto operator()(vk::Offset3D&& v) const {
		return forward_as_tuple(v.x, v.y, v.z);
	}
};
template<> struct tuplefier<vk::Rect2D> {
	inline auto operator()(vk::Rect2D&& v) const {
		return forward_as_tuple(v.extent, v.offset);
	}
};
template<> struct tuplefier<vk::SamplerCreateInfo> {
	inline auto operator()(vk::SamplerCreateInfo&& rhs) const {
		return forward_as_tuple(
			rhs.flags,
			rhs.magFilter,
			rhs.minFilter,
			rhs.mipmapMode,
			rhs.addressModeU,
			rhs.addressModeV,
			rhs.addressModeW,
			rhs.mipLodBias,
			rhs.anisotropyEnable,
			rhs.maxAnisotropy,
			rhs.compareEnable,
			rhs.compareOp,
			rhs.minLod,
			rhs.maxLod,
			rhs.borderColor,
			rhs.unnormalizedCoordinates);
	}
};
template<> struct tuplefier<vk::SpecializationMapEntry> {
	inline auto operator()(vk::SpecializationMapEntry&& v) const {
		return forward_as_tuple(v.constantID, v.offset, v.size);
	}
};
template<> struct tuplefier<vk::StencilOpState> {
	inline auto operator()(vk::StencilOpState&& v) const {
		return forward_as_tuple(
			v.failOp,
			v.passOp,
			v.depthFailOp,
			v.compareOp,
			v.compareMask,
			v.writeMask,
			v.reference);
	}
};
template<> struct tuplefier<vk::PipelineVertexInputStateCreateInfo> {
	inline auto operator()(vk::PipelineVertexInputStateCreateInfo&& v) const {
		return forward_as_tuple(
				v.flags,
				span(v.pVertexBindingDescriptions,
				v.vertexBindingDescriptionCount),
				span(v.pVertexAttributeDescriptions,
				v.vertexAttributeDescriptionCount));
	}
};
template<> struct tuplefier<vk::PipelineColorBlendAttachmentState> {
	inline auto operator()(vk::PipelineColorBlendAttachmentState&& v) const {
		return forward_as_tuple(
			v.blendEnable,
			v.srcColorBlendFactor,
			v.dstColorBlendFactor,
			v.colorBlendOp,
			v.srcAlphaBlendFactor,
			v.dstAlphaBlendFactor,
			v.alphaBlendOp,
			v.colorWriteMask);
	}
};
template<> struct tuplefier<vk::PipelineDepthStencilStateCreateInfo> {
	inline auto operator()(vk::PipelineDepthStencilStateCreateInfo&& v) const {
    return forward_as_tuple(
			v.flags,
			v.depthTestEnable,
			v.depthWriteEnable,
			v.depthCompareOp,
			v.depthBoundsTestEnable,
			v.stencilTestEnable,
			v.front,
			v.back,
			v.minDepthBounds,
			v.maxDepthBounds);
	}
};
template<> struct tuplefier<vk::PipelineInputAssemblyStateCreateInfo> {
	inline auto operator()(vk::PipelineInputAssemblyStateCreateInfo&& v) const {
		return forward_as_tuple(v.flags, v.topology, v.primitiveRestartEnable);
	}
};
template<> struct tuplefier<vk::PipelineMultisampleStateCreateInfo> {
	inline auto operator()(vk::PipelineMultisampleStateCreateInfo&& v) const {
    return forward_as_tuple(
			v.flags,
			v.rasterizationSamples,
			v.sampleShadingEnable,
			v.minSampleShading,
			v.pSampleMask,
			v.alphaToCoverageEnable,
			v.alphaToOneEnable);
	}
};
template<> struct tuplefier<vk::PipelineRasterizationStateCreateInfo> {
	inline auto operator()(vk::PipelineRasterizationStateCreateInfo&& v) const {
		return forward_as_tuple(
			v.flags,
			v.depthClampEnable,
			v.rasterizerDiscardEnable,
			v.polygonMode,
			v.cullMode, 
			v.frontFace,
			v.depthBiasEnable,
			v.depthBiasConstantFactor,
			v.depthBiasClamp,
			v.depthBiasSlopeFactor,
			v.lineWidth);
	}
};
template<> struct tuplefier<vk::PipelineViewportStateCreateInfo> {
	inline auto operator()(vk::PipelineViewportStateCreateInfo&& v) const {
		return forward_as_tuple(
			v.flags,
			span(v.pViewports, v.viewportCount),
			span(v.pScissors, v.scissorCount));
	}
};
template<> struct tuplefier<vk::PushConstantRange> {
	inline auto operator()(vk::PushConstantRange&& rhs) const {
		return forward_as_tuple(
			rhs.stageFlags,
			rhs.offset,
			rhs.size);
	}
};
template<> struct tuplefier<vk::Viewport> {
	inline auto operator()(vk::Viewport&& v) const {
		return forward_as_tuple(
			v.x,
			v.y,
			v.width,
			v.height,
			v.minDepth,
			v.maxDepth);
	}
};
template<> struct tuplefier<vk::VertexInputAttributeDescription> {
	inline auto operator()(vk::VertexInputAttributeDescription&& v) const {
		return forward_as_tuple(
			v.location,
			v.binding,
			v.format,
			v.offset);
	}
};
template<> struct tuplefier<vk::VertexInputBindingDescription> {
	inline auto operator()(vk::VertexInputBindingDescription&& v) const {
		return forward_as_tuple(
			v.binding,
			v.stride,
			v.inputRate);
	}
};

static_assert(!tuplefiable<VkExtent2D>);
static_assert(tuplefiable<vk::Extent2D>);
static_assert(tuplefiable<vk::SamplerCreateInfo>);

}