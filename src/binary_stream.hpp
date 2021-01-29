#pragma once

namespace stm {

template<class T> concept can_resize = requires(T c, size_t s) { c.resize(s); };
template<class T, class E> concept can_emplace_empty = requires(T c) { c.emplace({}); };

class binary_stream {
private:
	fstream mStream;
	
public:
	inline binary_stream(const fs::path& filepath) : mStream(fstream(filepath)) {}

	inline binary_stream& read(void* data, size_t size) { mStream.read(reinterpret_cast<char*>(data), size); return *this; }
	inline binary_stream& write(const void* data, size_t size) { mStream.write(reinterpret_cast<const char*>(data), size); return *this; }

	template<typename T> requires(is_trivially_copyable_v<T>) inline T read() { T v; read(&v, sizeof(T)); return v; }
	template<typename T> requires(is_trivially_copyable_v<T>) inline binary_stream& read(T& v) { return read(&v, sizeof(T)); }
	template<typename T> requires(is_trivially_copyable_v<T>) inline binary_stream& write(const T& v) { return write(&v, sizeof(T)); }

	template<typename T> inline binary_stream& operator<<(const T& v) {
		if constexpr (ranges::sized_range<T>) {
			write(v.size());
			if constexpr (ranges::contiguous_range<T> && is_trivially_copyable_v<T::value_type>)
				write(v.data(), v.size()*sizeof(T::value_type));
			else
				for (const auto& i : v) operator<<(i);
		} else if constexpr (specialization_of<pair, T>::value) {
			operator<<(v.first);
			operator<<(v.second);
		} else if constexpr (specialization_of<tuple, T>::value)
			apply(operator<<, v);
		else
			write(v);
		return *this;
	}
	template<typename T> inline binary_stream& operator>>(T& v) {
		if constexpr (ranges::sized_range<T>) {
			size_t n = read<T::size_type>();
			if constexpr (can_resize<ranges::sized_range, T>)
				v.resize(n);
			else if constexpr (can_emplace_empty<T>)
				for (size_t i = 0; i < n; i++) v.emplace({});
			// TODO: stack/queue/priority_queue won't resize correctly here 
			if constexpr (ranges::contiguous_range<T> && is_trivially_copyable_v<T::value_type>)
				read(v.data(), v.size()*sizeof(T::value_type));
			else
				for (auto& i : v) operator>>(i);
		} else if constexpr (specialization_of<pair, T>) {
			operator>>(v.first);
			operator>>(v.second);
		} if constexpr (specialization_of<tuple, T>)
			apply(operator>>, v);
		else
			read(v);
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