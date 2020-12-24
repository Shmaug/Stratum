#pragma once

namespace stm {

template<ranges::range R>
class valrange : public R {
public:
	using value_type = ranges::range_value_t<R>;
	using const_reference = const value_type&;

	inline valrange& operator=(const_reference rhs) { ranges::fill(*this, rhs); return *this; }

	inline bool operator==(const valrange& s) const { return ranges::equal(*this, s); }
	inline bool operator!=(const valrange& s) const { return !ranges::equal(*this, s); }
	inline bool operator==(const_reference s) const { return ranges::all_of(*this, [&](const auto& i) { return i == s; }); }
	inline bool operator!=(const_reference s) const { return ranges::any_of(*this, [&](const auto& i) { return i != s; }); }

	inline valrange operator!() const { valrange v; ranges::transform(*this, ranges::begin(v), logical_not<value_type>()); return v; }
	inline valrange operator~() const { valrange v; ranges::transform(*this, ranges::begin(v), bit_not<value_type>()); return v; }
	inline valrange operator-() const { valrange v; ranges::transform(*this, ranges::begin(v), negate<value_type>()); return v; }
	
	inline valrange& operator+=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), plus<value_type>()); return *this; }
	inline valrange& operator-=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), minus<value_type>()); return *this; }
	inline valrange& operator*=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), multiplies<value_type>()); return *this; }
	inline valrange& operator/=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), divides<value_type>()); return *this; }
	inline valrange& operator%=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), modulus<value_type>()); return *this; }
	inline valrange& operator|=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), bit_or<value_type>()); return *this; }
	inline valrange& operator&=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), bit_and<value_type>()); return *this; }
	inline valrange& operator^=(const valrange& rhs) { ranges::transform(*this, rhs, ranges::begin(*this), bit_xor<value_type>()); return *this; }
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

template<class R> concept value_range = is_specialization_v<R, valrange>;

template<value_range R> inline R min(const R& x, const R& y) { valrange v; ranges::transform(x, ranges::begin(v), min<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R max(const R& x, const R& y) { valrange v; ranges::transform(x, ranges::begin(v), max<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R clamp(const R& x, const R& xmin, const R& xmax) { valrange v; ranges::transform(x, ranges::begin(v), clamp<ranges::range_value_t<R>>); return v; }

template<value_range R> inline R abs(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), abs<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R ceil(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), ceil<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R floor(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), floor<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R sqrt(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), sqrt<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R exp(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), exp<ranges::range_value_t<R>>); return v; }

template<value_range R> inline R pow(const R& x, const R& y) { valrange v; ranges::transform(x, ranges::begin(v), pow<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R pow(const R& x, const ranges::range_value_t<R>& y) { valrange v; ranges::transform(x, ranges::begin(v), [&](const ranges::range_value_t<R>& i){return pow(i,y);}); return v; }
template<value_range R> inline R pow(const ranges::range_value_t<R>& x, const R& y) { valrange v; ranges::transform(y, ranges::begin(v), [&](const ranges::range_value_t<R>& i){return pow(x,i);}); return v; }


template<value_range R> inline R cos(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), cos<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R sin(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), sin<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R tan(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), tan<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R acos(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), acos<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R asin(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), asin<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R atan(const R& x) { valrange v; ranges::transform(x, ranges::begin(v), atan<ranges::range_value_t<R>>); return v; }
template<value_range R> inline R atan2(const R& y, const R& x) { valrange v; ranges::transform(x, ranges::begin(v), atan2<ranges::range_value_t<R>>); return v; }

}