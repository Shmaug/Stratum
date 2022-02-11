#pragma once

#include "common.hpp"

namespace stm {

template<typename Tx, typename Ty> void binary_read(istream&, pair<Tx, Ty>&);
template<typename Tx, typename Ty> void binary_write(ostream&, const pair<Tx, Ty>&);
template<typename...Types> void binary_read(istream&, tuple<Types...>&);
template<typename...Types> void binary_write(ostream&, const tuple<Types...>&) ;
template<ranges::range R> void binary_read(istream&, R&);
template<ranges::range R> void binary_write(ostream&, const R&);

template<typename T> requires(is_trivially_copyable_v<T>)
inline void binary_read(istream& lhs, T& rhs) {
	lhs.read(reinterpret_cast<char*>(&rhs), sizeof(T));
}
template<typename T> requires(is_trivially_copyable_v<T>)
inline void binary_write(ostream& lhs, const T& rhs) {
	lhs.write(reinterpret_cast<const char*>(&rhs), sizeof(T));
}

#define DECLARE_VK_CAST_READ_WRITE(T)\
inline void binary_read (istream& lhs,       vk::T& rhs) { binary_read (lhs, (      Vk##T&)rhs); }\
inline void binary_write(ostream& lhs, const vk::T& rhs) { binary_write(lhs, (const Vk##T&)rhs); }
DECLARE_VK_CAST_READ_WRITE(AttachmentDescription)
DECLARE_VK_CAST_READ_WRITE(ComponentMapping)
DECLARE_VK_CAST_READ_WRITE(Extent2D)
DECLARE_VK_CAST_READ_WRITE(Extent3D)
DECLARE_VK_CAST_READ_WRITE(Offset2D)
DECLARE_VK_CAST_READ_WRITE(Offset3D)
DECLARE_VK_CAST_READ_WRITE(ImageSubresourceLayers)
DECLARE_VK_CAST_READ_WRITE(ImageSubresourceRange)
DECLARE_VK_CAST_READ_WRITE(PipelineColorBlendAttachmentState)
DECLARE_VK_CAST_READ_WRITE(PushConstantRange)
DECLARE_VK_CAST_READ_WRITE(Rect2D)
DECLARE_VK_CAST_READ_WRITE(SpecializationMapEntry)
DECLARE_VK_CAST_READ_WRITE(StencilOpState)
DECLARE_VK_CAST_READ_WRITE(Viewport)
DECLARE_VK_CAST_READ_WRITE(VertexInputAttributeDescription)
DECLARE_VK_CAST_READ_WRITE(VertexInputBindingDescription)
#undef DECLARE_VK_CAST_READ_WRITE

template<typename Tx, typename Ty>
inline void binary_read(istream& lhs, pair<Tx, Ty>& rhs) {
	binary_read(lhs, rhs.first);
	binary_read(lhs, rhs.second);
}
template<typename Tx, typename Ty>
inline void binary_write(ostream& lhs, const pair<Tx, Ty>& rhs) {
	binary_write(lhs, rhs.first);
	binary_write(lhs, rhs.second);
}

template<typename...Types>
inline void binary_read(istream& lhs, tuple<Types...>& rhs) {
	apply([&](Types&... args){
		(binary_read(lhs, args), ...);
	}, rhs);
}
template<typename...Types>
inline void binary_write(ostream& lhs, const tuple<Types...>& rhs) {
	apply([&](const Types&... args){
		(binary_write(lhs, args), ...);
	}, rhs);
}

template<ranges::range R> requires(ranges::contiguous_range<R> || is_specialization_v<R, list>)
inline void binary_read(istream& lhs, R& rhs) {
	size_t n;
	if constexpr (fixed_sized_range<R>)
		n = ranges::size(rhs);
	else {
		binary_read(lhs, n);
		rhs.resize(n);
	}
	if constexpr (is_trivially_copyable_v<ranges::range_value_t<R>>)
		lhs.read(reinterpret_cast<char*>(ranges::data(rhs)), n*sizeof(ranges::range_value_t<R>));
	else
		for (size_t i = 0; i < n; i++) {
			remove_const_tuple_t<ranges::range_value_t<R>> val;
			binary_read(lhs, val);
			rhs.emplace_back(move(val));
		}
}
template<ranges::range R> requires(is_specialization_v<R, forward_list>)
inline void binary_read(istream& lhs, R& rhs) {
	size_t n;
	binary_read(lhs, n);
	for (size_t i = 0; i < n; i++) {
		remove_const_tuple_t<ranges::range_value_t<R>> val;
		binary_read(lhs, val);
		rhs.emplace_front(move(val));
	}
	rhs.reverse();
}
template<associative_range R>
inline void binary_read(istream& lhs, R& rhs) {
	size_t n;
	binary_read(lhs, n);
	for (size_t i = 0; i < n; i++) {
		remove_const_tuple_t<ranges::range_value_t<R>> val;
		binary_read(lhs, val);
		rhs.emplace(move(val));
	}
}

template<ranges::range R>
inline void binary_write(ostream& lhs, const R& rhs) {
	if constexpr (!fixed_sized_range<R>) 
		binary_write(lhs, ranges::size(rhs));
	if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
		lhs.write(reinterpret_cast<const char*>(ranges::data(rhs)), ranges::size(rhs)*sizeof(ranges::range_value_t<R>));
	else
		for (const ranges::range_value_t<R>& v : rhs)
			binary_write(lhs, v);
}

}