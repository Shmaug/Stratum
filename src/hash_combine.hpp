#pragma once

#include "byte_blob.hpp"
#include "tuplefier.hpp"

namespace stm {

template<typename T> concept Hashable = requires(T a) { { std::hash<T>()(a) } -> convertible_to<size_t>; };

template<typename Tx, typename... Ty>
inline size_t hash_combine(const Tx& tx, const Ty&... ty) {
  size_t x = hash<Tx>()(tx);
	size_t y;
	if constexpr(sizeof...(Ty) == 0)
		return x;
	else if constexpr(sizeof...(Ty) == 1)
  	y = hash<Ty...>()(ty...);
	else
		y = hash_combine(ty...);
  return x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
}

template<typename _Tuple, size_t... Inds>
inline size_t hash_tuple(const _Tuple& t, index_sequence<Inds...>) {
	return hash_combine(get<Inds>(t)...);
}

template<typename _Tuple>
inline size_t hash_tuple(const _Tuple& t) {
	return hash_tuple(t, make_index_sequence<tuple_size_v<_Tuple>-1>());
}

// accepts string literals
template<typename T, size_t N>
constexpr size_t hash_array(const T(& arr)[N], size_t n = N-1) {
  return (n <= 1) ? hash_combine(arr[0]) : hash_combine(hash_array(arr, n-1), arr[n-1]);
}

}

namespace std {

template<stm::Hashable Tx, stm::Hashable Ty>
struct hash<pair<Tx, Ty>> {
	constexpr size_t operator()(const pair<Tx, Ty>& p) const { return stm::hash_combine(p.first, p.second); }
};
template<stm::Hashable... Args>
struct hash<tuple<Args...>> {
	constexpr size_t operator()(const tuple<Args...>& t) const { return stm::hash_tuple(t); }
};
template<stm::tuplefiable T>
struct hash<T> {
	constexpr size_t operator()(const T& t) const {
		return stm::hash_tuple(stm::tuplefier<T>()(forward<T>( const_cast<T&>(t) )));
	}
};

template<stm::Hashable T, size_t N> struct hash<std::array<T,N>> {
	constexpr size_t operator()(const std::array<T,N>& a) const { return stm::hash_array<T,N>(a.data()); }
};
template<ranges::range R> requires(stm::Hashable<ranges::range_value_t<R>>)
struct hash<R> {
	inline size_t operator()(const R& r) const {
		size_t h = 0;
		for (const auto& i : r) h = stm::hash_combine(h, i);
		return h;
	}
};

template<>
struct hash<stm::byte_blob> {
	inline size_t operator()(const stm::byte_blob& b) const {
		return hash<vector<byte>>()((vector<byte>)b);
	}
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

template<typename BitType> requires(convertible_to<typename vk::Flags<BitType>::MaskType, size_t>)
struct hash<vk::Flags<BitType>> {
	inline size_t operator()(vk::Flags<BitType> rhs) const {
		return (typename vk::Flags<BitType>::MaskType)rhs;
	}
};

static_assert(stm::Hashable<size_t>);
static_assert(stm::Hashable<string>);
static_assert(stm::Hashable<vector<seed_seq::result_type>>);
static_assert(stm::Hashable<unordered_map<string,vk::SpecializationMapEntry>>);
static_assert(stm::Hashable<vk::DeviceSize>);
static_assert(stm::Hashable<vk::Format>);
static_assert(stm::Hashable<vk::SampleCountFlags>);
static_assert(stm::Hashable<vk::SampleCountFlagBits>);
static_assert(stm::Hashable<vk::ShaderStageFlagBits>);

}