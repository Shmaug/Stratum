#pragma once

namespace stm {

class byte_blob {
private:
	vector<std::byte> mData;

public:
	inline bool empty() const { return mData.empty(); }
	inline void clear() { mData.clear(); }
	inline void resize(size_t sz) { mData.resize(sz); }
	inline size_t size() const { return mData.size(); }
	inline std::byte* data() { return mData.data(); }
	inline const std::byte* data() const { return mData.data(); }

	inline byte_blob() = default;
	inline byte_blob(const byte_blob& cp) = default;
	inline byte_blob(byte_blob&& mv) = default;
	template<typename T, size_t N> inline byte_blob(const array<T,N>& cp) : mData(vector<std::byte>(sizeof(T)*N)) { memcpy(data(), cp.data(), sizeof(T)*N); }
	inline byte_blob(size_t size, const void* data) : mData(vector<std::byte>((const std::byte*)data, (const std::byte*)data + size)) {}
	
	inline byte_blob& operator=(const byte_blob& rhs) = default;
	inline byte_blob& operator=(byte_blob&& rhs) = default;
	template<typename T> inline byte_blob& operator=(const T& rhs) {
		resize(sizeof(T));
		memcpy(data(), &rhs, size())
		return *this;
	}

	inline bool operator==(const byte_blob& rhs) const { return size() == rhs.size() && (empty() || (memcmp(data(), rhs.data(), size()) == 0)); }
	template<typename T> inline bool operator==(const T& rhs) const { return size() == sizeof(T) && *reinterpret_cast<T*>(data()) == rhs; }

	template<typename T> inline explicit operator T() { return *reinterpret_cast<T*>(data()); }
};

}