#pragma once

namespace stm {

class binary_stream {
private:
	fstream mStream;
	
public:
	inline binary_stream(const fs::path& filepath) : mStream(fstream(filepath)) {}

	inline binary_stream& read(void* data, size_t size) { mStream.read(reinterpret_cast<char*>(data), size); return *this; }
	inline binary_stream& write(const void* data, size_t size) { mStream.write(reinterpret_cast<const char*>(data), size); return *this; }

	template<typename T> inline T read() {
		static_assert(is_standard_layout<T>::value, "T binary_stream::read<T>() must be explicity specialized for non-standard-layout types");
		T v;
		read(&v, sizeof(T));
		return v;
	}
	template<typename T> inline binary_stream& read(T& v) {
		static_assert(is_standard_layout<T>::value, "binary_stream& binary_stream::read(T) must be explicity specialized for non-standard-layout types");
		return read(&v, sizeof(T));
	}
	template<typename T> inline binary_stream& write(const T& v) {
		static_assert(is_standard_layout<T>::value, "binary_stream& binary_stream::write(const T&) must be explicity specialized for non-standard-layout types");
		return write(&v, sizeof(T));
	}

	template<typename T> inline binary_stream& operator<<(const T& v) {
		if constexpr (_Is_allocator<T>::value) {
			write(v.size());
			if constexpr (is_specialization<T, vector>::value && is_integral<T::value_type>::value)
				write(v.data(), v.size()*sizeof(T::value_type));
			else
				for (const auto& i : v) operator<<(i);
		} else {
			if constexpr (is_specialization<T, tuple>::value)
				apply(operator<<, v);
			else if constexpr (is_specialization<T, pair>::value) {
				operator<<(v.first);
				operator<<(v.second);
			} else
				write(&v, sizeof(T));
		}
		return *this;
	}
	template<typename T> inline binary_stream& operator>>(T& v) {
		if constexpr (_Is_allocator<T>::value) {
			if constexpr (is_specialization<T, vector>::value)
				v.resize(read<T::size_type>());
			else {
				auto n = read<T::size_type>();
				for (T::size_type i = 0; i < n; i++) v.emplace({});
			}
			if constexpr (is_specialization<T, vector>::value && is_integral<T::value_type>::value)
				read(v.data(), v.size()*sizeof(T::value_type));
			else
				for (auto& i : v) operator>>(i);
		} else {
			if constexpr (is_specialization<T, tuple>::value)
				apply(operator>>, v);
			else if constexpr (is_specialization<T, pair>::value) {
				operator>>(v.first);
				operator>>(v.second);
			} else
				read(&v, sizeof(T));
		}
		return *this;
	}
	
	inline binary_stream& operator<<(const string& v) {
		write(v.length());
		write(v.data(), v.length());
		return *this;
	}
	inline binary_stream& operator>>(string& v) {
		size_t l = read<size_t>();
		v.resize(l);
		read(v.data(), l);
		return *this;
	}

	inline operator fstream&() { return mStream; } 
};

template<typename T> inline binary_stream& operator<<(binary_stream& stream, const vector<T>& v) {
	stream << v.size();
	for (const auto& item : v) stream << item;
	return stream;
}
template<typename T> inline binary_stream& operator>>(binary_stream& stream, vector<T>& v) {
	size_t sz;
	stream >> sz;
	v.resize(sz);
	for (auto& item : v) stream >> item;
	return stream;
}

template<typename T> inline binary_stream& operator<<(binary_stream& stream, const set<T>& v) {
	stream << v.size();
	for (const auto& item : v) stream << item;
	return stream;
}
template<typename T> inline binary_stream& operator>>(binary_stream& stream, set<T>& v) {
	size_t sz;
	stream >> sz;
	for (uint32_t i = 0; i < sz; i++) {
		T item;
		stream >> item;
		v.insert(item);
	}
	return stream;
}

template<typename Tx, typename Ty> inline binary_stream& operator<<(binary_stream& stream, const map<Tx, Ty>& v) {
	stream << v.size();
	for (const auto&[key,value] : v) {
		stream << key;
		stream << value;
	}
	return stream;
}
template<typename Tx, typename Ty> inline binary_stream& operator>>(binary_stream& stream, map<Tx, Ty>& v) {
	size_t sz;
	stream >> sz;
	for (uint32_t i = 0; i < sz; i++) {
		Tx x;
		Ty y;
		stream >> x;
		stream >> y;
		v.insert_or_assign(x, y);
	}
	return stream;
}

template<typename Tx, typename Ty> inline binary_stream& operator<<(binary_stream& stream, const multimap<Tx, Ty>& v) {
	stream << v.size();
	for (const auto&[key,value] : v) {
		stream << key;
		stream << value;
	}
	return stream;
}
template<typename Tx, typename Ty> inline binary_stream& operator>>(binary_stream& stream, multimap<Tx, Ty>& v) {
	size_t sz;
	stream >> sz;
	for (uint32_t i = 0; i < sz; i++) {
		Tx x;
		Ty y;
		stream >> x;
		stream >> y;
		v.insert({ x, y });
	}
	return stream;
}

}