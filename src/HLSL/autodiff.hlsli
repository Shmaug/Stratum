#ifndef AUTODIFF_H
#define AUTODIFF_H

template<typename Real, uint M, uint N>
inline matrix<Real,M,N> outer_prod(const vector<Real,M> u, const vector<Real,N> v) {
	matrix<Real,M,N> r;
	for (uint i = 0; i < M; i++)
		for (uint j = 0; j < N; j++)
			r[i][j] = u[i]*v[j];
	return r;
}

template<typename Real, uint N, uint DerivativeLevel>
class DiffScalarN {
	typedef DiffScalarN<Real, N, DerivativeLevel> diff_scalar_type;

	Real value;
	vector<Real, N> grad;
	matrix<Real, N, N> hess;

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
			result.hess += outer_prod<Real,N,N>(grad, rhs.grad);
			result.hess += outer_prod<Real,N,N>(rhs.grad, grad);
		}
		
		return result;
	}

	inline diff_scalar_type inverse() {
		const Real valueSqr = value*value;
    const Real valueCub = valueSqr * value;
    const Real invValueSqr = 1 / valueSqr;
		// vn = 1/v
		diff_scalar_type result;
		result.value = 1 / value;

		// Dvn = -1/(v^2) Dv
		if (DerivativeLevel > 0) 
			result.grad = grad * -invValueSqr;

		// D^2vn = -1/(v^2) D^2v + 2/(v^3) Dv Dv^T
		if (DerivativeLevel > 1) {
			result.hess = hess * -invValueSqr;
			result.hess += outer_prod<Real,N,N>(grad, grad) * (2 / valueCub);
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

	inline bool operator<(const Real s) {
		return value < s;
	}
	
	inline bool operator<=(const Real s) {
		return value <= s;
	}

	inline bool operator>(const Real s) {
		return value > s;
	}
	
	inline bool operator>=(const Real s) {
		return value >= s;
	}

	inline bool operator==(const Real s) {
		return value == s;
	}

	inline bool operator!=(const Real s) {
		return value != s;
	}
};

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> make_diff_scalar(const Real v, const uint var_index = -1) {
	DiffScalarN<Real, N, DerivativeLevel> r;
	r.value = v;
	r.grad = 0;
	if (var_index != -1) r.grad[var_index] = 1;
	r.hess = 0;
	return r;
}


/// ================= Misc Operations =================
template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> sqrt(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real sqrtVal = sqrt(s.value);
	const Real temp    = 1 / (2 * sqrtVal);

	// vn = sqrt(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = sqrtVal;

	// Dvn = 1/(2 sqrt(v)) Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * temp;

	// D^2vn = 1/(2 sqrt(v)) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * temp;
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * (-1 / (4 * s.value * sqrtVal));
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> pow(const DiffScalarN<Real, N, DerivativeLevel> s, const Real a) {
	const Real powVal = pow(s.value, a);
	const Real temp   = a * pow(s.value, a-1);
	// vn = v ^ a
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = powVal;

	// Dvn = a*v^(a-1) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * temp;

	// D^2vn = a*v^(a-1) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * temp;
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * (a * (a-1) * pow(s.value, a-2));
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> exp(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real expVal = exp(s.value);

	// vn = exp(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = expVal;

	// Dvn = exp(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * expVal;

	// D^2vn = exp(v) * Dv*Dv^T + exp(v) * D^2v
	if (DerivativeLevel > 0)
		result.hess = (outer_prod<Real,N,N>(s.grad, s.grad) + s.hess) * expVal;

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> log(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real logVal = log(s.value);

	// vn = log(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = logVal;

	// Dvn = Dv / v
	if (DerivativeLevel > 0)
		result.grad = s.grad / s.value;

	// D^2vn = (v*D^2v - Dv*Dv^T)/(v^2)
	if (DerivativeLevel > 0)
		result.hess = s.hess / s.value - (outer_prod<Real,N,N>(s.grad, s.grad) / (s.value*s.value));

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> sin(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real sinVal = sin(s.value);
	const Real cosVal = cos(s.value);
	// vn = sin(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = sinVal;

	// Dvn = cos(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * cosVal;

	// D^2vn = -sin(v) * Dv*Dv^T + cos(v) * Dv^2
	if (DerivativeLevel > 1) {
		result.hess = s.hess * cosVal;
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * (-sinVal);
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> cos(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real sinVal = sin(s.value);
	const Real cosVal = cos(s.value);
	// vn = cos(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = cosVal;

	// Dvn = -sin(v) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * -sinVal;

	// D^2vn = -cos(v) * Dv*Dv^T - sin(v) * Dv^2
	if (DerivativeLevel > 1) {
		result.hess = s.hess * -sinVal;
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * (-cosVal);
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> acos(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real temp = -sqrt(1 - s.value*s.value);

	// vn = acos(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = acos(s.value);

	// Dvn = -1/sqrt(1-v^2) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * (1 / temp);

	// D^2vn = -1/sqrt(1-v^2) * D^2v - v/[(1-v^2)^(3/2)] * Dv*Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * (1 / temp);
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * s.value / (temp*temp*temp);
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> asin(const DiffScalarN<Real, N, DerivativeLevel> s) {
	const Real temp = sqrt(1 - s.value*s.value);

	// vn = asin(v)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = asin(s.value);

	// Dvn = 1/sqrt(1-v^2) * Dv
	if (DerivativeLevel > 0)
		result.grad = s.grad * (1 / temp);

	// D^2vn = 1/sqrt(1-v*v) * D^2v + v/[(1-v^2)^(3/2)] * Dv*Dv^T
	if (DerivativeLevel > 1) {
		result.hess = s.hess * (1 / temp);
		result.hess += outer_prod<Real,N,N>(s.grad, s.grad) * s.value / (temp*temp*temp);
	}

	return result;
}

template<typename Real, uint N, uint DerivativeLevel>
inline DiffScalarN<Real, N, DerivativeLevel> atan2(const DiffScalarN<Real, N, DerivativeLevel> y, const DiffScalarN<Real, N, DerivativeLevel> x) {
	// vn = atan2(y, x)
	DiffScalarN<Real, N, DerivativeLevel> result;
	result.value = atan2(y.value, x.value);

	// Dvn = (x*Dy - y*Dx) / (x^2 + y^2)
	if (DerivativeLevel > 0) {
		const Real denom = x.value*x.value + y.value*y.value;
		result.grad = y.grad * (x.value / denom) - x.grad * (y.value / denom);
	}

	// D^2vn = (Dy*Dx^T + xD^2y - Dx*Dy^T - yD^2x) / (x^2+y^2)
	//    - [(x*Dy - y*Dx) * (2*x*Dx + 2*y*Dy)^T] / (x^2+y^2)^2
	if (DerivativeLevel > 1) {
		const Real denom = x.value*x.value + y.value*y.value;
		const Real denomSqr = denom*denom;

		result.hess = (y.hess*x.value 
			+ outer_prod(y.grad, x.grad)
			- x.hess*y.value 
			- outer_prod(x.grad, y.grad)
		) / denom;

		result.hess -= outer_prod<Real,N,N>(
			y.grad*(x.value/denomSqr) - x.grad*(y.value/denomSqr),
			x.grad*(2 * x.value) + y.grad*(2 * y.value)
		);
	}

	return result;
}

#endif