#pragma once

namespace stm {

class byte_blob {
private:
	vector<char> mData;

public:
	inline byte_blob() = default;
	inline byte_blob(const byte_blob& cp) = default;
	inline byte_blob(byte_blob&& mv) = default;
	inline byte_blob& operator=(const byte_blob& rhs) = default;
	inline byte_blob& operator=(byte_blob&& rhs) = default;
	
	inline explicit byte_blob(size_t sz) : mData(vector<char>(sz)) {}

	template<ranges::contiguous_range R> requires(is_trivially_copyable_v<ranges::range_value_t<R>>)
	inline byte_blob(const R& r) : mData(vector<char>(reinterpret_cast<const char*>(ranges::data(r)), reinterpret_cast<const char*>(ranges::data(r)+ranges::size(r)))) {}
	
	inline bool empty() const { return mData.empty(); }
	inline void clear() { mData.clear(); }
	inline void resize(size_t sz) { mData.resize(sz); }
	inline size_t size() const { return mData.size(); }
	inline char* data() { return mData.data(); }
	inline const char* data() const { return mData.data(); }

	template<typename T> inline byte_blob& operator=(const T& rhs) {
		resize(sizeof(T));
		memcpy(data(), &rhs, size());
		return *this;
	}

	inline bool operator==(const byte_blob& rhs) const { return size() == rhs.size() && (empty() || (memcmp(data(), rhs.data(), size()) == 0)); }
	template<typename T> inline bool operator==(const T& rhs) const { return size() == sizeof(T) && *reinterpret_cast<T*>(data()) == rhs; }

	template<typename T> requires(is_specialization_v<T, vector> || is_trivially_copyable_v<T>)
	inline explicit operator T&() {
		if constexpr (is_specialization_v<T, vector>)
			return static_cast<T>(mData);
		else
			return *reinterpret_cast<T*>(data());
	}
};

}