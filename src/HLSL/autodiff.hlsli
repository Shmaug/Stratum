#ifndef AUTODIFF_H
#define AUTODIFF_H

template<uint M, uint N>
inline matrix<float,M,N> outer_prod(const vector<float,M> u, const vector<float,N> v) {
	matrix<float,M,N> r;
	for (uint i = 0; i < M; i++)
		for (uint j = 0; j < N; j++)
			r[i][j] = u[i]*v[j];
	return r;
}

template<typename T, uint N, uint DerivativeLevel>
class DiffScalarN {
	typedef DiffScalarN<T, N, DerivativeLevel> diff_scalar_type;

	T value;
	vector<T, N> grad;
	matrix<T, N, N> hess;

	inline diff_scalar_type operator+(const diff_scalar_type rhs) {
		diff_scalar_type s;
		s.value = value + rhs.value;
		s.grad  = grad  + rhs.grad;
		s.hess  = hess  + rhs.hess;
		return s;
	}

	inline diff_scalar_type operator-(const diff_scalar_type rhs) {
		diff_scalar_type s;
		s.value = value - rhs.value;
		s.grad  = grad  - rhs.grad;
		s.hess  = hess  - rhs.hess;
		return s;
	}

	inline diff_scalar_type operator-() {
		diff_scalar_type s;
		s.value = -value;
		s.grad  = -grad;
		s.hess  = -hess;
		return s;
	}

	inline diff_scalar_type operator*(const diff_scalar_type rhs) {
		diff_scalar_type result;
		result.value = value*rhs.value;

		// Product rule
		if (DerivativeLevel > 0)
			result.grad = rhs.grad * value + grad * rhs.value;

		// (i,j) = g*F_xixj + g*G_xixj + F_xi*G_xj + F_xj*G_xi
		if (DerivativeLevel > 1) {
			result.hess = rhs.hess * value;	
			result.hess += hess * rhs.value;	
			result.hess += outer_prod<T,N,N>(grad, rhs.grad);
			result.hess += outer_prod<T,N,N>(rhs.grad, grad);
		}
		
		return result;
	}

	inline diff_scalar_type inverse() {
		const T valueSqr = value*value;
    const T valueCub = valueSqr * value;
    const T invValueSqr = 1 / valueSqr;
		// vn = 1/v
		diff_scalar_type result;
		result.value = 1 / value;

		// Dvn = -1/(v^2) Dv
		if (DerivativeLevel > 0) 
			result.grad = grad * -invValueSqr;

		// D^2vn = -1/(v^2) D^2v + 2/(v^3) Dv Dv^T
		if (DerivativeLevel > 1) {
			result.hess = hess * -invValueSqr;
			result.hess += outer_prod<T,N,N>(grad, grad) * (2 / valueCub);
		}

		return result;
	}
	
	inline diff_scalar_type operator/(const diff_scalar_type rhs) { return operator*(rhs.inverse()); }

	inline bool operator<(const diff_scalar_type s) {
		return value < s.value;
	}
	
	inline bool operator<=(const diff_scalar_type s) {
		return value <= s.value;
	}

	inline bool operator>(const diff_scalar_type s) {
		return value > s.value;
	}
	
	inline bool operator>=(const diff_scalar_type s) {
		return value >= s.value;
	}

	inline bool operator<(const T s) {
		return value < s;
	}
	
	inline bool operator<=(const T s) {
		return value <= s;
	}

	inline bool operator>(const T s) {
		return value > s;
	}
	
	inline bool operator>=(const T s) {
		return value >= s;
	}

	inline bool operator==(const T s) {
		return value == s;
	}

	inline bool operator!=(const T s) {
		return value != s;
	}
};

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> make_diff_scalar(const T v, const uint var_index = -1) {
	DiffScalarN<T, N, DerivativeLevel> r;
	r.value = v;
	r.grad = 0;
	if (var_index != -1) r.grad[var_index] = 1;
	r.hess = 0;
	return r;
}


/// ================= Misc Operations =================
template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> sqrt(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T sqrtVal = sqrt(s.value);
	const T temp    = 1 / (2 * sqrtVal);

	// vn = sqrt(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = sqrtVal;

	// Dvn = 1/(2 sqrt(v)) Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * temp;

	// D^2vn = 1/(2 sqrt(v)) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * temp;
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * (-1 / (4 * s.value * sqrtVal));
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> pow(const DiffScalarN<T, N, DerivativeLevel> s, const T a) {
	const T powVal = pow(s.value, a);
	const T temp   = a * pow(s.value, a-1);
	// vn = v ^ a
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = powVal;

	// Dvn = a*v^(a-1) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * temp;

	// D^2vn = a*v^(a-1) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * temp;
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * (a * (a-1) * pow(s.value, a-2));
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> exp(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T expVal = exp(s.value);

	// vn = exp(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = expVal;

	// Dvn = exp(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * expVal;

	// D^2vn = exp(v) * Dv*Dv^T + exp(v) * D^2v
	if (DerivativeLevel > 0)
		result.hess = (outer_prod<T,N,N>(s.grad, s.grad) + s.hess) * expVal;

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> log(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T logVal = log(s.value);

	// vn = log(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = logVal;

	// Dvn = Dv / v
	if (DerivativeLevel > 0)
		result.grad = s.grad / s.value;

	// D^2vn = (v*D^2v - Dv*Dv^T)/(v^2)
	if (DerivativeLevel > 0)
		result.hess = s.hess / s.value - (outer_prod<T,N,N>(s.grad, s.grad) / (s.value*s.value));

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> sin(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T sinVal = sin(s.value);
	const T cosVal = cos(s.value);
	// vn = sin(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = sinVal;

	// Dvn = cos(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * cosVal;

	// D^2vn = -sin(v) * Dv*Dv^T + cos(v) * Dv^2
	if (DerivativeLevel > 1) {
		result.hess = s.hess * cosVal;
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * (-sinVal);
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> cos(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T sinVal = sin(s.value);
	const T cosVal = cos(s.value);
	// vn = cos(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = cosVal;

	// Dvn = -sin(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * -sinVal;

	// D^2vn = -cos(v) * Dv*Dv^T - sin(v) * Dv^2
	if (DerivativeLevel > 1) {
		result.hess = s.hess * -sinVal;
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * (-cosVal);
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> acos(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T temp = -sqrt(1 - s.value*s.value);

	// vn = acos(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = acos(s.value);

	// Dvn = -1/sqrt(1-v^2) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * (1 / temp);

	// D^2vn = -1/sqrt(1-v^2) * D^2v - v/[(1-v^2)^(3/2)] * Dv*Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * (1 / temp);
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * s.value / (temp*temp*temp);
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> asin(const DiffScalarN<T, N, DerivativeLevel> s) {
	const T temp = sqrt(1 - s.value*s.value);

	// vn = asin(v)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = asin(s.value);

	// Dvn = 1/sqrt(1-v^2) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * (1 / temp);

	// D^2vn = 1/sqrt(1-v*v) * D^2v + v/[(1-v^2)^(3/2)] * Dv*Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * (1 / temp);
		result.hess += outer_prod<T,N,N>(s.grad, s.grad) * s.value / (temp*temp*temp);
	}

	return result;
}

template<typename T, uint N, uint DerivativeLevel>
inline DiffScalarN<T, N, DerivativeLevel> atan2(const DiffScalarN<T, N, DerivativeLevel> y, const DiffScalarN<T, N, DerivativeLevel> x) {
	// vn = atan2(y, x)
	DiffScalarN<T, N, DerivativeLevel> result;
	result.value = atan2(y.value, x.value);

	// Dvn = (x*Dy - y*Dx) / (x^2 + y^2)
	if (DerivativeLevel > 0) {
		const T denom = x.value*x.value + y.value*y.value;
		result.grad = y.grad * (x.value / denom) - x.grad * (y.value / denom);
	}

	// D^2vn = (Dy*Dx^T + xD^2y - Dx*Dy^T - yD^2x) / (x^2+y^2)
	//    - [(x*Dy - y*Dx) * (2*x*Dx + 2*y*Dy)^T] / (x^2+y^2)^2
	if (DerivativeLevel > 1) {
		const T denom = x.value*x.value + y.value*y.value;
		const T denomSqr = denom*denom;

		result.hess = (y.hess*x.value 
			+ outer_prod(y.grad, x.grad)
			- x.hess*y.value 
			- outer_prod(x.grad, y.grad)
		) / denom;

		result.hess -= outer_prod<T,N,N>(
			y.grad*(x.value/denomSqr) - x.grad*(y.value/denomSqr),
			x.grad*(2 * x.value) + y.grad*(2 * y.value)
		);
	}

	return result;
}

#endif