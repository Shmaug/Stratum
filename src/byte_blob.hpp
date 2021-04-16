#pragma once

namespace stm {

using byte = std::byte;

class byte_blob {
private:
	vector<byte> mData;

public:
	inline byte_blob() = default;
	inline byte_blob(const byte_blob& cp) = default;
	inline byte_blob(byte_blob&& mv) = default;
	inline byte_blob& operator=(const byte_blob& rhs) = default;
	inline byte_blob& operator=(byte_blob&& rhs) = default;
	
	inline byte_blob(size_t size) : mData(vector<byte>(size)) {}

	template<ranges::sized_range R> requires(is_trivially_copyable_v<ranges::range_value_t<R>>)
	inline byte_blob(R&& r) {
		mData = vector<byte>(r.size()*sizeof(ranges::range_value_t<R>));
		if constexpr (ranges::contiguous_range<R>)
			memcpy(mData.data(), ranges::data(r), mData.size());
		else {
			size_t offset = 0;
			for (const ranges::range_value_t<R>& v : r) {
				memcpy(mData.data() + offset, &v, sizeof(ranges::range_value_t<R>));
				offset += sizeof(ranges::range_value_t<R>);
			}
		}
	}
	
	inline bool empty() const { return mData.empty(); }
	inline void clear() { mData.clear(); }
	inline void resize(size_t sz) { mData.resize(sz); }
	inline size_t size() const { return mData.size(); }
	inline byte* data() { return mData.data(); }
	inline const byte* data() const { return mData.data(); }

	inline bool operator==(const byte_blob& rhs) const { return size() == rhs.size() && (empty() || (memcmp(data(), rhs.data(), size()) == 0)); }
	template<typename T> inline bool operator==(const T& rhs) const { return size() == sizeof(T) && *reinterpret_cast<T*>(data()) == rhs; }

	inline operator bool() const { return !mData.empty(); }
	
	template<typename T> requires(is_specialization_v<T, vector> || is_trivially_copyable_v<T>)
	inline explicit operator T&() {
		if constexpr (is_specialization_v<T, vector>)
			return static_cast<T>(mData);
		else
			return *reinterpret_cast<T*>(data());
	}
};

}