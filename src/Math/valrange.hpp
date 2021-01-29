#pragma once

namespace stm {

template <class T, template <class...> class Template>
struct specialization_of : std::false_type {};
template <template <class...> class Template, class... Args>
struct specialization_of<Template<Args...>, Template> : std::true_type {};


template<ranges::range R, class Fn, ranges::range... Args>
R transform_values(Fn fn, const Args&... args) {
	R dst;
	auto dst_end = ranges::end(dst);
	auto args_it = make_tuple((ranges::begin(args), ...));
	for (auto it = ranges::begin(dst); it != dst_end; it++) {
		*it = apply(fn, args_it);
		apply([](auto& it){ it++; }, args_it);
	}
	return dst;
}

template<ranges::range R>
class valrange : public R {
public:
	using range_type = R;
	using value_type = ranges::range_value_t<R>;
	using const_reference = const value_type&;

	//inline valrange& operator=(const_reference rhs) { ranges::fill(*this, rhs); return *this; }

	inline bool operator==(const valrange& s) const { return ranges::equal(*this, s); }
	inline bool operator!=(const valrange& s) const { return !ranges::equal(*this, s); }
	inline bool operator==(const_reference s) const { return ranges::all_of(*this, [&](const auto& i) { return i == s; }); }
	inline bool operator!=(const_reference s) const { return ranges::any_of(*this, [&](const auto& i) { return i != s; }); }

	inline valrange operator!() const { valrange v; ranges::transform(*this, ranges::begin(v), logical_not<value_type>()); return v; }
	inline valrange operator~() const { valrange v; ranges::transform(*this, ranges::begin(v), bit_not<value_type>()); return v; }
	inline valrange operator-() const { valrange v; ranges::transform(*this, ranges::begin(v), negate<value_type>()); return v; }
	
	inline valrange& operator+=(const valrange& rhs) { transform_values(*this, plus<value_type>(), rhs); return *this; }
	inline valrange& operator-=(const valrange& rhs) { transform_values(*this, minus<value_type>(), rhs); return *this; }
	inline valrange& operator*=(const valrange& rhs) { transform_values(*this, multiplies<value_type>(), rhs); return *this; }
	inline valrange& operator/=(const valrange& rhs) { transform_values(*this, divides<value_type>(), rhs); return *this; }
	inline valrange& operator%=(const valrange& rhs) { transform_values(*this, modulus<value_type>(), rhs); return *this; }
	inline valrange& operator|=(const valrange& rhs) { transform_values(*this, bit_or<value_type>(), rhs); return *this; }
	inline valrange& operator&=(const valrange& rhs) { transform_values(*this, bit_and<value_type>(), rhs); return *this; }
	inline valrange& operator^=(const valrange& rhs) { transform_values(*this, bit_xor<value_type>(), rhs); return *this; }
	inline valrange operator+(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), plus<value_type>()); return v; }
	inline valrange operator-(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), minus<value_type>()); return v; }
	inline valrange operator*(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), multiplies<value_type>()); return v; }
	inline valrange operator/(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), divides<value_type>()); return v; }
	inline valrange operator%(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), modulus<value_type>()); return v; }
	inline valrange operator|(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), bit_or<value_type>()); return v; }
	inline valrange operator&(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), bit_and<value_type>()); return v; }
	inline valrange operator^(const valrange& rhs) const { valrange v; ranges::transform(*this, rhs, ranges::begin(v), bit_xor<value_type>()); return v; }

	inline valrange& operator+=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i+rhs; }); return *this; }
	inline valrange& operator-=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i-rhs; }); return *this; }
	inline valrange& operator*=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i*rhs; }); return *this; }
	inline valrange& operator/=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i/rhs; }); return *this; }
	inline valrange& operator%=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i%rhs; }); return *this; }
	inline valrange& operator|=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i|rhs; }); return *this; }
	inline valrange& operator&=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i&rhs; }); return *this; }
	inline valrange& operator^=(const_reference rhs) { ranges::transform(*this, ranges::begin(*this), [&](const_reference i){ return i^rhs; }); return *this; }
	inline valrange operator+(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i+rhs; }); return v; }
	inline valrange operator-(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i-rhs; }); return v; }
	inline valrange operator*(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i*rhs; }); return v; }
	inline valrange operator/(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i/rhs; }); return v; }
	inline valrange operator%(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i%rhs; }); return v; }
	inline valrange operator|(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i|rhs; }); return v; }
	inline valrange operator&(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i&rhs; }); return v; }
	inline valrange operator^(const_reference rhs) const { valrange v; ranges::transform(*this, ranges::begin(v), [&](const_reference i) { return i^rhs; }); return v; }

	template<ranges::sized_range Ry> requires requires(Ry r, size_t i) { r.resize(i); }
	inline explicit operator Ry() const { 
		Ry dst;
		if (dst.size() != R::size()) dst.resize(R::size());
		ranges::transform(*this, ranges.begin(dst), [](const_reference i) { return static_cast<const Ry::value_type>(i); });
		return dst;
	}
};


template<typename T>
concept valrange_specialization = specialization_of<T, valrange>::value;

template<valrange_specialization R> inline R min(const R& x, const R& y) { return transform_values(min<R::value_type>, x, y); }
template<valrange_specialization R> inline R max(const R& x, const R& y) { return transform_values(max<R::value_type>, x, y); }

template<valrange_specialization R> inline R abs(const R& x) { R v; ranges::transform(x, ranges::begin(v), abs<R::value_type>); return v; }
template<valrange_specialization R> inline R ceil(const R& x) { R v; ranges::transform(x, ranges::begin(v), ceil<R::value_type>); return v; }
template<valrange_specialization R> inline R floor(const R& x) { R v; ranges::transform(x, ranges::begin(v), floor<R::value_type>); return v; }
template<valrange_specialization R> inline R sqrt(const R& x) { R v; ranges::transform(x, ranges::begin(v), sqrt<R::value_type>); return v; }
template<valrange_specialization R> inline R exp(const R& x) { R v; ranges::transform(x, ranges::begin(v), exp<R::value_type>); return v; }

template<valrange_specialization R> inline R pow(const R& x, const R& y) { R v; ranges::transform(x, ranges::begin(v), pow<R::value_type>); return v; }
template<valrange_specialization R> inline R pow(const R& x, const ranges::range_value_t<R>& y) { R v; ranges::transform(x, ranges::begin(v), [&](const R::value_type& i){return pow(i,y);}); return v; }
template<valrange_specialization R> inline R pow(const ranges::range_value_t<R>& x, const R& y) { R v; ranges::transform(y, ranges::begin(v), [&](const R::value_type& i){return pow(x,i);}); return v; }


template<valrange_specialization R> inline R cos(const R& x) { R v; ranges::transform(x, ranges::begin(v), cos<R::value_type>); return v; }
template<valrange_specialization R> inline R sin(const R& x) { R v; ranges::transform(x, ranges::begin(v), sin<R::value_type>); return v; }
template<valrange_specialization R> inline R tan(const R& x) { R v; ranges::transform(x, ranges::begin(v), tan<R::value_type>); return v; }
template<valrange_specialization R> inline R acos(const R& x) { R v; ranges::transform(x, ranges::begin(v), acos<R::value_type>); return v; }
template<valrange_specialization R> inline R asin(const R& x) { R v; ranges::transform(x, ranges::begin(v), asin<R::value_type>); return v; }
template<valrange_specialization R> inline R atan(const R& x) { R v; ranges::transform(x, ranges::begin(v), atan<R::value_type>); return v; }
template<valrange_specialization R> inline R atan2(const R& y, const R& x) { R v; ranges::transform(x, ranges::begin(v), atan2<R::value_type>); return v; }

}

namespace std {
	template<stm::valrange_specialization R>
	struct hash<R> { inline size_t operator()(const R& r) const { return hash<R::range_type>()(r); } };
}