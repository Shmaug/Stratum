#pragma once

#include "tuplefier.hpp"

namespace stm {

template<typename T>
struct static_assert_t {
	constexpr static_assert_t() {
		static_assert(sizeof(T) == -1,  __FUNCTION__);
	}
};

template<typename underlying_stream>
class byte_stream {
private:
	underlying_stream mStream;
	
public:
	template<typename... Types>
	inline byte_stream(Types&&... args) : mStream(underlying_stream(forward<Types>(args)...)) {}
	
	inline operator underlying_stream&() { return mStream; } 

	inline byte_stream& read(void* data, size_t size) { mStream.read(reinterpret_cast<char*>(data), size); return *this; }
	inline byte_stream& write(const void* data, size_t size) { mStream.write(reinterpret_cast<const char*>(data), size); return *this; }

	template<typename T> inline byte_stream& operator>>(T& rhs) {
		auto read_fn = [&]<typename...Types>(Types... x){ (..., operator>> <Types> (x)); };
		if constexpr (is_trivially_copyable_v<T>)
			read(&rhs, sizeof(T));
		else if constexpr (is_specialization_v<T, pair>)
			read_fn(rhs.first, rhs.second);
		else if constexpr (is_specialization_v<T, tuple>)
			apply(read_fn, rhs);
		else if constexpr (tuplefiable<T>)
			apply(read_fn, tuplefier<T>()(forward<T>(rhs)));
		else
			static_assert_t<T>();
		return *this;
	}
	template<resizable_range R> inline byte_stream& operator>>(R& rhs) {
		size_t n;
		operator>>(n);
		rhs.resize(n);
		if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
			read(ranges::data(rhs), n*sizeof(ranges::range_value_t<R>));
		else
			for (auto& v : rhs) operator>>(v);
	}
	template<ranges::range R> inline byte_stream& operator>>(R& rhs) {
		size_t n;
		operator>>(n);
		for (size_t i = 0; i < n; i++) {
			remove_const_tuple_t<ranges::range_value_t<R>> v;
			operator>>(v);
			rhs.emplace(move(v));
		}
	}

	template<typename T> inline byte_stream& operator<<(const T& rhs) {
		auto write_fn = [&]<typename...Types>(Types... x){ (..., operator<< <Types> (x)); };
		if constexpr (is_trivially_copyable_v<T>)
			write(&rhs, sizeof(T));
		else if constexpr (is_specialization_v<T, pair>)
			write_fn(rhs.first, rhs.second);
		else if constexpr (is_specialization_v<T, tuple>)
			apply(write_fn, rhs);
		else if constexpr (tuplefiable<T>)
			apply(write_fn, tuplefier<T>()(forward<T>( const_cast<T&>(rhs) )));
		else
			static_assert_t<T>();
		return *this;
	}
	template<ranges::range R> inline byte_stream& operator<<(R& rhs) {
		size_t n;
		operator<<(n);
		if constexpr (ranges::contiguous_range<R> && is_trivially_copyable_v<ranges::range_value_t<R>>)
			write(ranges::data(rhs), ranges::size(rhs)*sizeof(ranges::range_value_t<R>));
		else
			for (const auto& v : rhs) operator<<(v);
		return *this;
	}
};

static_assert(stream_insertable< byte_stream<fstream>, vk::SpecializationMapEntry >);
static_assert(stream_insertable< byte_stream<fstream>, string >);
static_assert(stream_insertable< byte_stream<fstream>, vector<char> >);
static_assert(stream_insertable< byte_stream<fstream>, ranges::range_value_t<vector<char>> >);
static_assert(stream_insertable< byte_stream<fstream>, pair<string,vk::SpecializationMapEntry> >);
static_assert(stream_insertable< byte_stream<fstream>, unordered_map<string, vk::SpecializationMapEntry> >);

}