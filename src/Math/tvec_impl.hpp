inline tvec() {}
inline tvec(const T& s) { rpt(i,N) v[i] = s; }
inline tvec(const tvec& s) { rpt(i,N) v[i] = s.v[i]; }

inline tvec& operator=(const T& s) { rpt(i,N) v[i] = s; return *this; }
inline tvec& operator=(const tvec& s) { rpt(i,N) v[i] = s.v[i]; return *this; }

inline tvec operator-() const {
	tvec r;
	rpt(i,N) r.v[i] = -v[i];
	return r;
}
inline tvec operator-(const T& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] - s;
	return r;
}
inline tvec operator -(const tvec& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] - s.v[i];
	return r;
}
inline tvec& operator -=(const T& s) { rpt(i,N) v[i] -= s; return *this; }
inline tvec& operator -=(const tvec& s) { rpt(i,N) v[i] -= s.v[i]; return *this; }
inline friend tvec operator -(const T a, const tvec& s) {
	tvec r;
	rpt(i,N) r.v[i] = a - s.v[i];
	return r;
}

inline tvec operator +(const T& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] + s;
	return r;
}
inline tvec operator +(const tvec& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] + s.v[i];
	return r;
}
inline tvec& operator +=(const T& s) { rpt(i,N) v[i] += s; return *this; }
inline tvec& operator +=(const tvec& s) { rpt(i,N) v[i] += s.v[i]; return *this; }
inline friend tvec operator +(const T a, const tvec& s) { return s + a; }

inline tvec operator *(const T& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] * s;
	return r;
}
inline tvec operator *(const tvec& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] * s.v[i];
	return r;
}
inline tvec& operator *=(const T& s) { rpt(i,N) v[i] *= s; return *this; }
inline tvec& operator *=(const tvec& s) { rpt(i,N) v[i] *= s.v[i]; return *this; }
inline friend tvec operator *(const T& a, const tvec& s) { return s * a; }

inline friend tvec operator /(const T& a, const tvec& s) {
	tvec r;
	rpt(i,N) r.v[i] = a / s.v[i];
	return r;
}
inline tvec operator /(const T& s) const {
	tvec r;
	if (std::is_integral<T>::value)
		rpt(i,N) r.v[i] = v[i] / s;
	else {
		T inv_s = 1/s;
		rpt(i,N) r.v[i] = v[i] * inv_s;
	}
	return r;
}
inline tvec operator /(const tvec& s) const {
	tvec r;
	rpt(i,N) r.v[i] = v[i] / s.v[i];
	return r;
}
inline tvec& operator /=(const T& s) {
	if (std::is_integral<T>::value)
		rpt(i,N) v[i] /= s;
	else {
		T inv_s = 1/s;
		rpt(i,N) v[i] *= inv_s;
	}
	return *this;
}
inline tvec& operator /=(const tvec& s) {
	rpt(i,N) v[i] /= s.v[i];
	return *this;
}

inline T& operator[](uint32_t i) { return v[i]; }
inline T operator[](uint32_t i) const { return v[i]; }

inline bool operator ==(const tvec& a) const { rpt(i,N) if (v[i] != a.v[i]) return false; return true; }
inline bool operator !=(const tvec& a) const { return !operator ==(a); }

template<typename S>
inline operator tvec<N,S>() const {
	tvec<N,S> r;
	rpt(i,N) r.v[i] = (S)v[i];
	return r;
}