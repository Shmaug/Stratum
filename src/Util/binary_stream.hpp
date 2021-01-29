#pragma once

namespace stm {

class binary_stream {
private:
	fstream mStream;
	
public:
	inline binary_stream(const fs::path& filepath, ios_base::openmode mode = ios_base::binary | ios_base::in | ios_base::out) : mStream(fstream(filepath, mode)) {}
	
	inline operator fstream&() { return mStream; } 

	inline binary_stream& read(void* data, size_t size) { mStream.read(reinterpret_cast<char*>(data), size); return *this; }
	inline binary_stream& write(const void* data, size_t size) { mStream.write(reinterpret_cast<const char*>(data), size); return *this; }
};

template<typename T> concept binary_stream_extractable = stream_extractable<binary_stream, T>;
template<typename T> concept binary_stream_insertable = stream_insertable<binary_stream, T>;

template<typename T> requires(is_trivially_copyable_v<T>) inline binary_stream& operator<<(binary_stream& lhs, const T& rhs) {
	lhs.write(&rhs, sizeof(T));
	return lhs;
}
template<typename T> requires(is_trivially_copyable_v<T>) inline binary_stream& operator>>(binary_stream& lhs, T& rhs) {
	lhs.read(&rhs, sizeof(T));
	return lhs;
}

template<binary_stream_extractable Tx, binary_stream_extractable Ty> inline binary_stream& operator>>(binary_stream& lhs, pair<Tx,Ty>& rhs) {
	lhs >> rhs.first;
	lhs >> rhs.second;
	return lhs;
}
template<binary_stream_insertable Tx, binary_stream_insertable Ty> inline binary_stream& operator<<(binary_stream& lhs, const pair<Tx,Ty>& rhs) {
	lhs << rhs.first;
	lhs << rhs.second;
	return lhs;
}

template<binary_stream_extractable... Types> inline binary_stream& operator>>(binary_stream& lhs, tuple<Types...>& rhs) {
	apply([&](auto& i){lhs >> i;}, rhs);
	return lhs;
}
template<binary_stream_insertable... Types> inline binary_stream& operator<<(binary_stream& lhs, const tuple<Types...>& rhs) {
	apply([&](const auto& i ){lhs << i;}, rhs);
	return lhs;
}


template<ranges::sized_range R> requires(binary_stream_insertable<ranges::range_value_t<R>>)
inline binary_stream& operator<<(binary_stream& lhs, const R& rhs) {
	if constexpr(!fixed_size_range<R>)
		lhs << ranges::size(rhs);
	if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
		lhs.write(ranges::data(rhs), ranges::size(rhs)*sizeof(ranges::range_value_t<R>));
	else
		for (const auto& i : rhs) lhs << i;
	return lhs;
}

template<fixed_size_range R> requires(binary_stream_extractable<ranges::range_value_t<R>>)
inline binary_stream& operator>>(binary_stream& lhs, R& rhs) {
	if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
		lhs.read(ranges::data(rhs), ranges::size(rhs)*sizeof(ranges::range_value_t<R>));
	else
		for (auto& i : rhs) lhs >> i;
	return lhs;
}
template<resizable_range R> requires(binary_stream_extractable<ranges::range_value_t<R>>)
inline binary_stream& operator>>(binary_stream& lhs, R& rhs) {
	size_t n;
	lhs >> n;
	rhs.resize(n);
	if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
		lhs.read(ranges::data(rhs), ranges::size(rhs)*sizeof(ranges::range_value_t<R>));
	else
		for (auto& i : rhs) lhs >> i;
	return lhs;
}

template<ranges::sized_range R> requires(binary_stream_extractable<remove_const_pair_t<ranges::range_value_t<R>>> && !resizable_range<R> && !fixed_size_range<R>)
inline binary_stream& operator>>(binary_stream& lhs, R& rhs) {
	size_t n;
	lhs >> n;
	for (size_t i = 0; i < n; i++) {
		remove_const_pair_t<ranges::range_value_t<R>> v;
		lhs >> v;
		rhs.insert(v);
	}
	return lhs;
}


inline binary_stream& operator<<(binary_stream& lhs, const vk::SpecializationMapEntry& rhs) {
	lhs << rhs.constantID;
	lhs << rhs.offset;
	lhs << rhs.size;
	return lhs;
}
inline binary_stream& operator>>(binary_stream& lhs, vk::SpecializationMapEntry& rhs) {
	lhs >> rhs.constantID;
	lhs >> rhs.offset;
	lhs >> rhs.size;
	return lhs;
}

inline binary_stream& operator<<(binary_stream& lhs, const vk::PushConstantRange& rhs) {
	lhs << rhs.stageFlags;
	lhs << rhs.offset;
	lhs << rhs.size;
	return lhs;
}
inline binary_stream& operator>>(binary_stream& lhs, vk::PushConstantRange& rhs) {
	lhs >> rhs.stageFlags;
	lhs >> rhs.offset;
	lhs >> rhs.size;
	return lhs;
}


}