#pragma once

#define _USE_MATH_DEFINES
#include <math.h>
#include <numeric>
#include <ranges>
#include <span>

#include <iostream>
#include <fstream>
#include <stdexcept>

#include <mutex>
#include <thread>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <ranges>

#define EIGEN_HAS_STD_RESULT_OF 0
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <unsupported/Eigen/BVH>
#include <unsupported/Eigen/CXX11/Tensor>

namespace stm {
using namespace std;
using namespace Eigen;

#ifdef WIN32
namespace fs = std::filesystem;
#endif
#ifdef __GNUC__
namespace fs = std::experimental::filesystem;
#endif

template<class _Type, template<class...> class _Template> constexpr inline bool is_specialization_v = false;
template<template<class...> class _Template, class... _Types> constexpr inline bool is_specialization_v<_Template<_Types...>, _Template> = true;

template<typename T> struct remove_const_pair {
	using type = remove_const_t<T>;
};
template<typename Tx, typename Ty> struct remove_const_pair<pair<Tx, Ty>> {
	using type = pair<remove_const_t<Tx>, remove_const_t<Ty>>;
};
template<typename T> using remove_const_pair_t = typename remove_const_pair<T>::type;

template<typename R> concept fixed_size_range = ranges::sized_range<R> && requires(R r) { tuple_size<R>::value; };
template<typename R> concept resizable_range = ranges::sized_range<R> && !fixed_size_range<R> && requires(R r, size_t n) { r.resize(n); };
template<typename S, typename T> concept stream_extractable = requires(S s, T t) { s >> t; };
template<typename S, typename T> concept stream_insertable = requires(S s, T t) { s << t; };
}

#include "hash_combine.hpp"
#include "binary_stream.hpp"
#include "byte_blob.hpp"

namespace stm {

using byte = std::byte;

using Vector2u = Vector<uint32_t,2>;
using Vector3u = Vector<uint32_t,3>;
using Vector4u = Vector<uint32_t,4>;
using Matrix2u = Matrix<uint32_t,2,2>;
using Matrix3u = Matrix<uint32_t,3,3>;
using Matrix4u = Matrix<uint32_t,4,4>;
using AlignedBox2u = AlignedBox<uint32_t,2>;
using AlignedBox3u = AlignedBox<uint32_t,3>;
template<typename Object> using BVH3f = KdBVH<float,3,Object>;
template<typename Object> using BVH3d = KdBVH<double,3,Object>;
using fRay = ParametrizedLine<float,3>;
using dRay = ParametrizedLine<double,3>;

namespace shader_interop {
	using uint = uint32_t;
	
	#define DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, SizeSuffix) \
		using TypeName##SizeSuffix = Vector##SizeSuffix##TypeSuffix; \
		using TypeName##SizeSuffix##x##SizeSuffix = Matrix##SizeSuffix##TypeSuffix;

	#define DECLARE_FIXED_SIZE_TYPES(TypeName, TypeSuffix) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 2) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 3) \
		DECLARE_FIXED_SIZE_TYPE(TypeName, TypeSuffix, 4)
	
	DECLARE_FIXED_SIZE_TYPES(float,f)
	DECLARE_FIXED_SIZE_TYPES(double,d)
	DECLARE_FIXED_SIZE_TYPES(int,i)
	DECLARE_FIXED_SIZE_TYPES(uint,u)
	
	#undef DECLARE_FIXED_SIZE_TYPES
	#undef DECLARE_FIXED_SIZE_TYPE
	
	#include "../Shaders/include/stratum.hlsli"
}

}


namespace std {

template<stm::Hashable Tx, stm::Hashable Ty> struct hash<pair<Tx, Ty>> {
	constexpr inline size_t operator()(const pair<Tx, Ty>& p) const { return stm::hash_combine(p.first, p.second); }
};
template<stm::Hashable... Args> struct hash<tuple<Args...>> {
	constexpr inline size_t operator()(const tuple<Args...>& t) const { return apply(stm::hash_combine, t); }
};
template<stm::Hashable T, size_t N> struct hash<std::array<T,N>> {
	constexpr inline size_t operator()(const std::array<T,N>& a) const { return stm::hash_array<T,N>(a.data()); }
};
template<stm::Hashable T, int M, int N, int... Args>
struct hash<Eigen::Array<T,M,N,Args...>> {
  inline size_t operator()(const Eigen::Array<T,M,N,Args...>& m) const {
		if constexpr (M != Eigen::Dynamic && N != Eigen::Dynamic)
			return stm::hash_array((std::array<T,M*N>)m);
		else {
			size_t seed = 0;
			for (size_t i = 0; i < m.size(); i++)
				seed = stm::hash_combine(seed, m.data()+i);
			return seed;
		}
  }
};

template<ranges::range R> requires(stm::Hashable<ranges::range_value_t<R>>)
struct hash<R> {
	inline size_t operator()(const R& r) const {
		size_t h = 0;
		for (const auto& i : r) h = stm::hash_combine(h, i);
		return h;
	}
};

template<typename BitType> struct hash<vk::Flags<BitType>> {
	inline size_t operator()(const vk::Flags<BitType>& rhs) const {
		return (VkFlags)rhs;
	}
};

template<> struct hash<vk::AttachmentDescription> {
	inline size_t operator()(const vk::AttachmentDescription& a) const {
		return stm::hash_combine(
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
template<> struct hash<vk::ComponentMapping> {
	inline size_t operator()(const vk::ComponentMapping& v) const {
		return stm::hash_combine(v.r, v.g, v.b, v.a);
	}
};
template<> struct hash<vk::PushConstantRange> {
	inline size_t operator()(const vk::PushConstantRange& rhs) const {
		return stm::hash_combine(rhs.stageFlags, rhs.offset, rhs.size);
	}
};
template<> struct hash<vk::SamplerCreateInfo> {
	inline size_t operator()(const vk::SamplerCreateInfo& rhs) const {
		return stm::hash_combine(
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
template<> struct hash<vk::SpecializationMapEntry> {
	inline size_t operator()(const vk::SpecializationMapEntry& v) const {
		return stm::hash_combine(v.constantID, v.offset, v.size);
	}
};
template<> struct hash<vk::StencilOpState> {
	inline size_t operator()(const vk::StencilOpState& v) const {
		return stm::hash_combine(v.failOp, v.passOp, v.depthFailOp, v.compareOp, v.compareMask, v.writeMask, v.reference);
	}
};

template<> struct hash<vk::Extent2D> {
	inline size_t operator()(const vk::Extent2D& v) const { return stm::hash_combine(v.width, v.height); }
};
template<> struct hash<vk::Extent3D> {
	inline size_t operator()(const vk::Extent3D& v) const { return stm::hash_combine(v.width, v.height, v.depth); }
};
template<> struct hash<vk::Offset2D> {
	inline size_t operator()(const vk::Offset2D& v) const { return stm::hash_combine(v.x, v.y); }
};
template<> struct hash<vk::Offset3D> {
	inline size_t operator()(const vk::Offset3D& v) const { return stm::hash_combine(v.x, v.y, v.z); }
};
template<> struct hash<vk::Rect2D> {
	inline size_t operator()(const vk::Rect2D& v) const { return stm::hash_combine(v.extent, v.offset); }
};
template<> struct hash<vk::Viewport> {
	inline size_t operator()(const vk::Viewport& v) const { return stm::hash_combine(v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth); }
};

template<> struct hash<vk::VertexInputAttributeDescription> {
	inline size_t operator()(const vk::VertexInputAttributeDescription& v) const {
		return stm::hash_combine(v.location, v.binding, v.format, v.offset);
	}
};
template<> struct hash<vk::VertexInputBindingDescription> {
	inline size_t operator()(const vk::VertexInputBindingDescription& v) const {
		return stm::hash_combine(v.binding, v.stride, v.inputRate);
	}
};
template<> struct hash<vk::PipelineVertexInputStateCreateInfo> {
	inline size_t operator()(const vk::PipelineVertexInputStateCreateInfo& v) const {
		return stm::hash_combine(v.flags, span(v.pVertexBindingDescriptions, v.vertexBindingDescriptionCount), span(v.pVertexAttributeDescriptions, v.vertexAttributeDescriptionCount));
	}
};
template<> struct hash<vk::PipelineColorBlendAttachmentState> {
	inline size_t operator()(const vk::PipelineColorBlendAttachmentState& v) const {
		return stm::hash_combine(v.blendEnable, v.srcColorBlendFactor, v.dstColorBlendFactor, v.colorBlendOp, v.srcAlphaBlendFactor, v.dstAlphaBlendFactor, v.alphaBlendOp, v.colorWriteMask);
	}
};
template<> struct hash<vk::PipelineDepthStencilStateCreateInfo> {
	inline size_t operator()(const vk::PipelineDepthStencilStateCreateInfo& v) const {
    return stm::hash_combine(v.flags, v.depthTestEnable, v.depthWriteEnable, v.depthCompareOp, v.depthBoundsTestEnable, v.stencilTestEnable, v.front, v.back, v.minDepthBounds, v.maxDepthBounds);
	}
};
template<> struct hash<vk::PipelineInputAssemblyStateCreateInfo> {
	inline size_t operator()(const vk::PipelineInputAssemblyStateCreateInfo& v) const {
		return stm::hash_combine(v.flags, v.topology, v.primitiveRestartEnable);
	}
};
template<> struct hash<vk::PipelineMultisampleStateCreateInfo> {
	inline size_t operator()(const vk::PipelineMultisampleStateCreateInfo& v) const {
    return stm::hash_combine(v.flags, v.rasterizationSamples, v.sampleShadingEnable, v.minSampleShading, v.pSampleMask, v.alphaToCoverageEnable, v.alphaToOneEnable);
	}
};
template<> struct hash<vk::PipelineRasterizationStateCreateInfo> {
	inline size_t operator()(const vk::PipelineRasterizationStateCreateInfo& v) const {
		return stm::hash_combine(v.flags,
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
	inline size_t operator()(const vk::PipelineViewportStateCreateInfo& v) const { return stm::hash_combine(v.flags, span(v.pViewports, v.viewportCount), span(v.pScissors, v.scissorCount)); }
};

}