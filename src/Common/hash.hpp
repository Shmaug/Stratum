#pragma once

#include "binary_io.hpp"

namespace stm {

template<typename T> concept hashable = requires(T v) {
	{ std::hash<T>()(v) } -> convertible_to<size_t>;
};

constexpr size_t hash_combine(size_t x, size_t y) {
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

template<typename T> requires(!hashable<T> && !ranges::range<T>)
inline size_t hash_args(const T& v) {
	class hash_streambuf : public basic_streambuf<char, char_traits<char>> {
	public:
		using base_t = basic_streambuf<char, char_traits<char>>;
		size_t mValue = 0;
		inline base_t::int_type overflow(base_t::int_type v) {
			mValue = hash_combine(mValue, v);
			return v;
		}
	};
	hash_streambuf h;
	ostream os(&h);
	binary_write(os, v);
	return h.mValue;
}

template<hashable T>
inline size_t hash_args(const T& v) {
	return hash<T>()(v);
}

template<ranges::range R> requires(!hashable<R>)
inline size_t hash_args(const R& r) {
	auto it = ranges::begin(r);
	if (it == ranges::end(r)) return 0;
	size_t h = hash_args(*it);
	while (++it != ranges::end(r))
		h = hash_combine(h, hash_args(*it));
	return h;
}

template<typename Tx, typename... Ty>
inline size_t hash_args(const Tx& x, const Ty&... y) {
	return hash_combine(hash_args<Tx>(x), hash_args<Ty...>(y...));
}

}

namespace std {

template<typename T>
struct hash<weak_ptr<T>> {
	inline size_t operator()(const weak_ptr<T>& p) const {
		return hash<shared_ptr<T>>()(p.lock());
	}
};

template<stm::hashable T, size_t N>
struct hash<std::array<T,N>> {
	constexpr size_t operator()(const std::array<T,N>& a) const {
		return stm::hash_array<T,N>(a.data());
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

template<typename BitType>
struct hash<vk::Flags<BitType>> {
	inline size_t operator()(const vk::Flags<BitType>& v) const {
		return (typename vk::Flags<BitType>::MaskType)v;
	}
};
template<>
struct hash<vk::SamplerCreateInfo> {
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
template<>
struct hash<vk::PipelineVertexInputStateCreateInfo> {
	inline size_t operator()(const vk::PipelineVertexInputStateCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			span(v.pVertexBindingDescriptions, v.vertexBindingDescriptionCount),
			span(v.pVertexAttributeDescriptions, v.vertexAttributeDescriptionCount));
	}
};
template<>
struct hash<vk::PipelineDepthStencilStateCreateInfo> {
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
template<>
struct hash<vk::PipelineInputAssemblyStateCreateInfo> {
	inline size_t operator()(const vk::PipelineInputAssemblyStateCreateInfo& v) const {
		return stm::hash_args(v.flags, v.topology, v.primitiveRestartEnable);
	}
};
template<>
struct hash<vk::PipelineMultisampleStateCreateInfo> {
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
template<>
struct hash<vk::PipelineRasterizationStateCreateInfo> {
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
template<>
struct hash<vk::PipelineViewportStateCreateInfo> {
	inline size_t operator()(const vk::PipelineViewportStateCreateInfo& v) const {
		return stm::hash_args(
			v.flags,
			span(v.pViewports, v.viewportCount),
			span(v.pScissors, v.scissorCount));
	}
};

}