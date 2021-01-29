#pragma once

namespace std {
	
template<typename T> concept Hashable = requires(T a) { { hash<T>()(a) } -> convertible_to<size_t>; };

template<Hashable Tx, Hashable Ty> struct hash<pair<Tx, Ty>> {
	constexpr inline size_t operator()(const pair<Tx, Ty>& p) const {
		if constexpr (convertible_to<Tx, size_t>)
			return (size_t)p.first ^ (hash<Ty>()(p.second) + 0x9e3779b9 + ((size_t)p.first << 6) + ((size_t)p.first >> 2));
		else
			return hash<pair<size_t, size_t>>()({ hash<Tx>()(p.first), hash<Ty>()(p.second) });
	}
};

template<Hashable... Args> struct hash<tuple<Args...>> {

	template<Hashable T> constexpr inline size_t hash_combine(const T& x) const { return hash<T>()(x); }

	template<Hashable T, class... Ty>
	constexpr inline size_t hash_combine(const T& x, const Ty&... rest) const { return hash<pair<T, size_t>>()(x, hash_combine(forward(rest...))); }

	constexpr inline size_t operator()(const tuple<Args...>& t) const { return apply(hash_combine, t); }
};

}

namespace stm {

template<Hashable... Args>
constexpr inline size_t hash_objects(const Args&... args) { return hash<tuple<Args...>>(make_tuple(forward(args...))); }

// accepts string literals
template<Hashable T, size_t N>
constexpr inline size_t hash_array(const array<T,N>& arr, size_t n = N-1) {
	return (n <= 1) ? hash_objects(arr[0]) : hash_objects(hash_array(arr, n-1), arr[n-1]);
}

}