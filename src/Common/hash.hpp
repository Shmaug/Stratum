#pragma once

#include "common.hpp"

namespace stm {

template<typename T> concept hashable = requires(T v) { { std::hash<T>()(v) } -> convertible_to<size_t>; };

constexpr size_t hash_combine(const size_t x, const size_t y) {
	return x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
}

// accepts string literals
template<typename T, size_t N>
constexpr size_t hash_array(const T(& arr)[N]) {
	hash<T> hasher;
	if constexpr (N == 0)
		return 0;
	else if constexpr (N == 1)
		return hasher(arr[0]);
	else
		return hash_combine(hash_array<T,N-1>(arr), hasher(arr[N-1]));
}

template<hashable Tx, hashable... Ty>
inline size_t hash_args(const Tx& x, const Ty&... y) {
	if constexpr (sizeof...(Ty) == 0)
		return hash<Tx>()(x);
	else
		return hash_combine(hash<Tx>()(x), hash_args<Ty...>(y...));
}

}

namespace std {

template<stm::hashable T0, stm::hashable T1>
struct hash<pair<T0,T1>> {
	inline size_t operator()(const pair<T0,T1>& v) const {
		return stm::hash_combine(hash<T0>()(v.first), hash<T1>()(v.second));
	}
};

template<stm::hashable... Types>
struct hash<tuple<Types...>> {
	inline size_t operator()(const tuple<Types...>& v) const {
		return stm::hash_args<Types...>(get<Types>(v)...);
	}
};

template<stm::hashable T, size_t N>
struct hash<std::array<T,N>> {
	constexpr size_t operator()(const std::array<T,N>& a) const {
		return stm::hash_array<T,N>(a.data());
	}
};

template<ranges::range R> requires(stm::hashable<ranges::range_value_t<R>>)
struct hash<R> {
	inline size_t operator()(const R& r) const {
		size_t h = 0;
		for (auto it = ranges::begin(r); it != ranges::end(r); ++it)
			h = stm::hash_combine(h, hash<ranges::range_value_t<R>>()(*it));
		return h;
	}
};

template<typename T>
struct hash<weak_ptr<T>> {
	inline size_t operator()(const weak_ptr<T>& p) const {
		return hash<shared_ptr<T>>()(p.lock());
	}
};
	
template<stm::hashable T, int Rows, int Cols, int Options, int MaxRows, int MaxCols> requires(Rows != Eigen::Dynamic && Cols != Eigen::Dynamic)
struct hash<Eigen::Array<T,Rows,Cols,Options,MaxRows,MaxCols>> {
  constexpr size_t operator()(const Eigen::Array<T,Rows,Cols,Options,MaxRows,MaxCols>& m) const {
		hash<T> hasher;
		size_t h = 0;
		for (size_t r = 0; r < Rows; r++)
			for (size_t c = 0; c < Cols; c++)
				h = stm::hash_combine(h, hasher(m[r][c]));
		return h;
  }
};
template<stm::hashable T, int Rows, int Cols, int Options, int MaxRows, int MaxCols> requires(Rows != Eigen::Dynamic && Cols != Eigen::Dynamic)
struct hash<Eigen::Matrix<T,Rows,Cols,Options,MaxRows,MaxCols>> {
  constexpr size_t operator()(const Eigen::Matrix<T,Rows,Cols,Options,MaxRows,MaxCols>& m) const {
		return hash<Eigen::Array<T,Rows,Cols,Options,MaxRows,MaxCols>>()(m);
  }
};

template<> struct hash<vk::StencilOpState> {
	inline size_t operator()(const vk::StencilOpState& v) const {
		return stm::hash_args(
			v.failOp,
			v.passOp,
			v.depthFailOp,
			v.compareOp,
			v.compareMask,
			v.writeMask,
			(uint32_t)v.reference);
	}
};
template<> struct hash<vk::VertexInputAttributeDescription> {
	inline size_t operator()(const vk::VertexInputAttributeDescription& v) const {
		return stm::hash_args(v.location, v.binding, v.format, v.offset);
	}
};
template<> struct hash<vk::VertexInputBindingDescription> {
	inline size_t operator()(const vk::VertexInputBindingDescription& v) const {
		return stm::hash_args(v.binding, v.stride, v.inputRate);
	}
};
template<> struct hash<vk::Viewport> {
	inline size_t operator()(const vk::Viewport& v) const {
		return stm::hash_args(v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth);
	}
};
template<> struct hash<vk::Extent2D> {
	inline size_t operator()(const vk::Extent2D& v) const {
		return stm::hash_args(v.width, v.height);
	}
};
template<> struct hash<vk::Extent3D> {
	inline size_t operator()(const vk::Extent3D& v) const {
		return stm::hash_args(v.width, v.height, v.depth);
	}
};
template<> struct hash<vk::Offset2D> {
	inline size_t operator()(const vk::Offset2D& v) const {
		return stm::hash_args(v.x, v.y);
	}
};
template<> struct hash<vk::Offset3D> {
	inline size_t operator()(const vk::Offset3D& v) const {
		return stm::hash_args(v.x, v.y, v.z);
	}
};
template<> struct hash<vk::Rect2D> {
	inline size_t operator()(const vk::Rect2D& v) const {
		return stm::hash_args(v.offset, v.extent);
	}
};

template<> struct hash<vk::AttachmentDescription> {
	inline size_t operator()(const vk::AttachmentDescription& v) const {
		return stm::hash_args(
			v.flags,
			v.format,
			v.samples,
			v.loadOp,
			v.storeOp,
			v.stencilLoadOp,
			v.stencilStoreOp,
			v.initialLayout,
			v.finalLayout);
	}
};
template<> struct hash<vk::ComponentMapping> {
	inline size_t operator()(const vk::ComponentMapping& v) const {
		return stm::hash_args(v.r, v.g, v.b, v.a);
	}
};
template<> struct hash<vk::DescriptorSetLayoutBinding> {
	inline size_t operator()(const vk::DescriptorSetLayoutBinding& v) const {
		return stm::hash_args(
			v.binding,
			v.descriptorType,
			v.descriptorCount,
			v.stageFlags,
			v.pImmutableSamplers);
	}
};
template<> struct hash<vk::ImageSubresourceLayers> {
	inline size_t operator()(const vk::ImageSubresourceLayers& v) const {
		return stm::hash_args(v.aspectMask, v.mipLevel, v.baseArrayLayer, v.layerCount);
	}
};
template<> struct hash<vk::ImageSubresourceRange> {
	inline size_t operator()(const vk::ImageSubresourceRange& v) const {
		return stm::hash_args(v.aspectMask, v.baseMipLevel, v.levelCount, v.baseArrayLayer, v.layerCount);
	}
};
template<> struct hash<vk::PipelineColorBlendAttachmentState> {
	inline size_t operator()(const vk::PipelineColorBlendAttachmentState& v) const {
		return stm::hash_args(
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
template<> struct hash<vk::PushConstantRange> {
	inline size_t operator()(const vk::PushConstantRange& v) const {
		return stm::hash_args(v.stageFlags, v.offset, v.size);
	}
};
template<> struct hash<vk::SpecializationMapEntry> {
	inline size_t operator()(const vk::SpecializationMapEntry& v) const {
		return stm::hash_args(v.constantID, v.offset, v.size);
	}
};

template<> struct hash<vk::PipelineVertexInputStateCreateInfo> {
	inline size_t operator()(const vk::PipelineVertexInputStateCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			span(v.pVertexBindingDescriptions, v.vertexBindingDescriptionCount),
			span(v.pVertexAttributeDescriptions, v.vertexAttributeDescriptionCount));
	}
};
template<> struct hash<vk::PipelineDepthStencilStateCreateInfo> {
	inline size_t operator()(const vk::PipelineDepthStencilStateCreateInfo& v) const {
		return stm::hash_args(
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
template<> struct hash<vk::PipelineInputAssemblyStateCreateInfo> {
	inline size_t operator()(const vk::PipelineInputAssemblyStateCreateInfo& v) const {
		return stm::hash_args(v.flags, v.topology, v.primitiveRestartEnable);
	}
};
template<> struct hash<vk::PipelineMultisampleStateCreateInfo> {
	inline size_t operator()(const vk::PipelineMultisampleStateCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			v.rasterizationSamples,
			v.sampleShadingEnable,
			v.minSampleShading,
			v.pSampleMask,
			v.alphaToCoverageEnable,
			v.alphaToOneEnable);
	}
};
template<> struct hash<vk::PipelineRasterizationStateCreateInfo> {
	inline size_t operator()(const vk::PipelineRasterizationStateCreateInfo& v) const {
		return stm::hash_args(
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
template<> struct hash<vk::PipelineViewportStateCreateInfo> {
	inline size_t operator()(const vk::PipelineViewportStateCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			span(v.pViewports, v.viewportCount),
			span(v.pScissors, v.scissorCount));
	}
};
template<> struct hash<vk::SamplerCreateInfo> {
	inline size_t operator()(const vk::SamplerCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			v.magFilter,
			v.minFilter,
			v.mipmapMode,
			v.addressModeU,
			v.addressModeV,
			v.addressModeW,
			v.mipLodBias,
			v.anisotropyEnable,
			v.maxAnisotropy,
			v.compareEnable,
			v.compareOp,
			v.minLod,
			v.maxLod,
			v.borderColor,
			v.unnormalizedCoordinates);
	}
};

}