#pragma once

namespace stm {

template<typename T> concept Hashable = requires(T a) {
  { std::hash<T>()(a) } -> convertible_to<size_t>;
};

template<convertible_to<size_t> T>
constexpr inline size_t hash_combine(const T& x) {
  return (size_t)x;
}
template<Hashable T> requires(!convertible_to<T,size_t>)
inline size_t hash_combine(const T& x) {
  return std::hash<T>()(x);
}

template<Hashable Tx, Hashable... Ty>
inline size_t hash_combine(const Tx& first, const Ty&... rest) {
  size_t x = hash_combine(first);
  size_t y = hash_combine(rest...);
  return x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
}

// accepts string literals
template<Hashable T, size_t N>
constexpr inline size_t hash_array(const T(& arr)[N], size_t n = N-1) {
  return (n <= 1) ?
    hash_combine(arr[0]) :
    hash_combine(hash_array(arr, n-1), arr[n-1]);
}

}