#ifndef AUTODIFF_H
#define AUTODIFF_H

template<typename Real>
class DiffScalar {
	typedef T       value_type;
	typedef VecType deriv1_type;
	typedef MatType deriv2_type;

	/// ====================  Addition  ===================
	inline DiffScalar operator+(const DiffScalar lhs, const DiffScalar rhs) {
		return DiffScalar(lhs.env, lhs.value+rhs.value, lhs.deriv1+rhs.deriv1, lhs.deriv2+rhs.deriv2);
	}

	inline DiffScalar operator+(const DiffScalar lhs, const value_type rhs) {
		return DiffScalar(lhs.env, lhs.value+rhs, lhs.deriv1, lhs.deriv2);
	}

	inline DiffScalar operator+(const value_type lhs, const DiffScalar rhs) {
		return DiffScalar(rhs.env, rhs.value+lhs, rhs.deriv1, rhs.deriv2);
	}

	inline DiffScalar& operator+=(const DiffScalar &s) {
		if (env == NULL) {
			env    = s.env;
			value  = s.value;
			deriv1 = s.deriv1;
			deriv2 = s.deriv2;
		} else {
			value  += s.value;
			deriv1 += s.deriv1;
			deriv2 += s.deriv2;
		}
		return *this;
	}

	inline DiffScalar& operator+=(const value_type &v) {
		value  += v;
		return *this;
	}

	/// ==================  Subtraction  ==================
	friend DiffScalar operator-(const DiffScalar &lhs, const DiffScalar &rhs) {
		return DiffScalar(lhs.env, lhs.value-rhs.value, lhs.deriv1-rhs.deriv1, lhs.deriv2-rhs.deriv2);
	}

	friend DiffScalar operator-(const DiffScalar &lhs, const value_type &rhs) {
		return DiffScalar(lhs.env, lhs.value-rhs, lhs.deriv1, lhs.deriv2);
	}

	friend DiffScalar operator-(const value_type &lhs, const DiffScalar &rhs) {
		return DiffScalar(rhs.env, lhs-rhs.value, -rhs.deriv1, -rhs.deriv2);
	}

	friend DiffScalar operator-(const DiffScalar &s) {
		return DiffScalar(s.env, -s.value, -s.deriv1, -s.deriv2);
	}

	inline DiffScalar& operator-=(const DiffScalar &s) {
		if (env == NULL) {
			env    = s.env;
			value  = -s.value;
			deriv1 = -s.deriv1;
			deriv2 = -s.deriv2;
		} else {
			value  -= s.value;
			deriv1 -= s.deriv1;
			deriv2 -= s.deriv2;
		}
		return *this;
	}

	inline DiffScalar& operator-=(const value_type &v) {
		value  -= v;
		return *this;
	}

	/// ===================  Division  ====================
	friend DiffScalar operator/(const DiffScalar &lhs, const value_type &rhs) {
		return DiffScalar(lhs.env, lhs.value/rhs, lhs.deriv1/rhs, lhs.deriv2/rhs);
	}

	friend DiffScalar operator/(const value_type &lhs, const DiffScalar &rhs) {
		return lhs * inverse(rhs);
	}

	friend DiffScalar operator/(const DiffScalar &lhs, const DiffScalar &rhs) {
		return lhs * inverse(rhs);
	}

	friend DiffScalar inverse(const DiffScalar &s) {
		value_type valueSqr = s.value*s.value,
			valueCub = valueSqr * s.value,
			invValueSqr = (T) 1 / valueSqr;
		// vn = 1/v
		DiffScalar result(s.env, (T) 1 / s.value);

		// Dvn = -1/(v^2) Dv
		if (s.env->getDerivativeLevel() > 0) 
			result.deriv1 = s.deriv1 * -invValueSqr;

		// D^2vn = -1/(v^2) D^2v + 2/(v^3) Dv Dv^T
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * -invValueSqr;;
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* ((T) 2 / valueCub);
		}

		return result;
	}

	inline DiffScalar& operator/=(const value_type &v) {
		value  /= v;
		deriv1 /= v;
		deriv2 /= v;
		return *this;
	}

	/// ================  Multiplication  =================
	friend DiffScalar operator*(const DiffScalar &lhs, const value_type &rhs) {
		return DiffScalar(lhs.env, lhs.value*rhs, lhs.deriv1*rhs, lhs.deriv2*rhs);
	}

	friend DiffScalar operator*(const value_type &lhs, const DiffScalar &rhs) {
		return DiffScalar(rhs.env, rhs.value*lhs, rhs.deriv1*lhs, rhs.deriv2*lhs);
	}

	friend DiffScalar operator*(const DiffScalar &lhs, const DiffScalar &rhs) {
		DiffScalar result(lhs.env, lhs.value*rhs.value);

		/// Product rule
		if (lhs.env->getDerivativeLevel() > 0) 
			result.deriv1 = rhs.deriv1 * lhs.value + lhs.deriv1 * rhs.value;

		// (i,j) = g*F_xixj + g*G_xixj + F_xi*G_xj + F_xj*G_xi
		if (lhs.env->getDerivativeLevel() > 1) {
			result.deriv2 = rhs.deriv2 * lhs.value;	
			result.deriv2 += lhs.deriv2 * rhs.value;	
			result.deriv2 += ublas::outer_prod(lhs.deriv1, rhs.deriv1);
			result.deriv2 += ublas::outer_prod(rhs.deriv1, lhs.deriv1);
		}
		
		return result;
	}

	inline DiffScalar& operator*=(const value_type &v) {
		value  *= v;
		deriv1 *= v;
		deriv2 *= v;
		return *this;
	}

	/// ================= Misc Operations =================
	friend DiffScalar sqrt(const DiffScalar &s) {
		value_type sqrtVal = std::sqrt(s.value),
                   temp    = (T) 1 /((T) 2 * sqrtVal);

		// vn = sqrt(v)
		DiffScalar result(s.env, sqrtVal);

		// Dvn = 1/(2 sqrt(v)) Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * temp;

		// D^2vn = 1/(2 sqrt(v)) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * temp;
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* (-(T) 1 / ((T) 4 * s.value * sqrtVal));
		}

		return result;
	}

	friend DiffScalar pow(const DiffScalar &s, const value_type &a) {
		value_type powVal = std::pow(s.value, a),
                   temp   = a * std::pow(s.value, a-1);
		// vn = v ^ a
		DiffScalar result(s.env, powVal);

		// Dvn = a*v^(a-1) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * temp;

		// D^2vn = a*v^(a-1) D^2v - 1/(4 v*sqrt(v)) Dv Dv^T
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * temp;
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* (a * (a-1) * std::pow(s.value, a-2));
		}

		return result;
	}
	
	friend DiffScalar exp(const DiffScalar &s) {
		value_type expVal = std::exp(s.value);

		// vn = exp(v)
		DiffScalar result(s.env, expVal);

		// Dvn = exp(v) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * expVal;

		// D^2vn = exp(v) * Dv*Dv^T + exp(v) * D^2v
		if (s.env->getDerivativeLevel() > 0)
			result.deriv2 = (ublas::outer_prod(s.deriv1, s.deriv1)
				+ s.deriv2) * expVal;

		return result;
	}

	friend DiffScalar log(const DiffScalar &s) {
		value_type logVal = std::log(s.value);

		// vn = log(v)
		DiffScalar result(s.env, logVal);

		// Dvn = Dv / v
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 / s.value;

		// D^2vn = (v*D^2v - Dv*Dv^T)/(v^2)
		if (s.env->getDerivativeLevel() > 0)
			result.deriv2 = s.deriv2 / s.value - 
			(ublas::outer_prod(s.deriv1, s.deriv1)
			/ (s.value*s.value));

		return result;
	}

	friend DiffScalar sin(const DiffScalar &s) {
		value_type sinVal = std::sin(s.value),
                   cosVal = std::cos(s.value);
		// vn = sin(v)
		DiffScalar result(s.env, sinVal);

		// Dvn = cos(v) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * cosVal;

		// D^2vn = -sin(v) * Dv*Dv^T + cos(v) * Dv^2
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * cosVal;
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* (-sinVal);
		}

		return result;
	}

	friend DiffScalar cos(const DiffScalar &s) {
		value_type sinVal = std::sin(s.value),
                   cosVal = std::cos(s.value);
		// vn = cos(v)
		DiffScalar result(s.env, cosVal);

		// Dvn = -sin(v) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * -sinVal;

		// D^2vn = -cos(v) * Dv*Dv^T - sin(v) * Dv^2
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * -sinVal;
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* (-cosVal);
		}

		return result;
	}

	friend DiffScalar acos(const DiffScalar &s) {
		if (std::abs(s.value) >= 1)
			throw std::range_error("acos: Expected a value in (-1, 1)");

		value_type temp = -std::sqrt((T) 1 - s.value*s.value);

		// vn = acos(v)
		DiffScalar result(s.env, std::acos(s.value));

		// Dvn = -1/sqrt(1-v^2) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * ((T) 1 / temp);

		// D^2vn = -1/sqrt(1-v^2) * D^2v - v/[(1-v^2)^(3/2)] * Dv*Dv^T
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * ((T) 1 / temp);
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* s.value / (temp*temp*temp);
		}

		return result;
	}

	friend DiffScalar asin(const DiffScalar &s) {
		if (std::abs(s.value) >= 1)
			throw std::range_error("asin: Expected a value in (-1, 1)");

		value_type temp = std::sqrt((T) 1 - s.value*s.value);

		// vn = asin(v)
		DiffScalar result(s.env, std::asin(s.value));

		// Dvn = 1/sqrt(1-v^2) * Dv
		if (s.env->getDerivativeLevel() > 0)
			result.deriv1 = s.deriv1 * ((T) 1 / temp);

		// D^2vn = 1/sqrt(1-v*v) * D^2v + v/[(1-v^2)^(3/2)] * Dv*Dv^T
		if (s.env->getDerivativeLevel() > 1) {
			result.deriv2 = s.deriv2 * ((T) 1 / temp);
			result.deriv2 += ublas::outer_prod(s.deriv1, s.deriv1) 
				* s.value / (temp*temp*temp);
		}

		return result;
	}

	friend DiffScalar atan2(const DiffScalar &y, const DiffScalar &x) {
		// vn = atan2(y, x)
		DiffScalar result(x.env, std::atan2(y.value, x.value));

		// Dvn = (x*Dy - y*Dx) / (x^2 + y^2)
		if (x.env->getDerivativeLevel() > 0) {
			value_type denom = x.value*x.value + y.value*y.value;
			result.deriv1 = y.deriv1 * (x.value / denom)
				-x.deriv1 * (y.value / denom);
		}

		// D^2vn = (Dy*Dx^T + xD^2y - Dx*Dy^T - yD^2x) / (x^2+y^2)
		//    - [(x*Dy - y*Dx) * (2*x*Dx + 2*y*Dy)^T] / (x^2+y^2)^2
		if (x.env->getDerivativeLevel() > 1) {
			value_type denom = x.value*x.value + y.value*y.value,
				denomSqr = denom*denom;

			result.deriv2 = (y.deriv2*x.value 
			 	+ ublas::outer_prod(y.deriv1, x.deriv1)
				- x.deriv2*y.value 
				- ublas::outer_prod(x.deriv1, y.deriv1)
			) / denom;

			result.deriv2 -= ublas::outer_prod(
				y.deriv1*(x.value/denomSqr) - x.deriv1*(y.value/denomSqr),
				x.deriv1*((T) 2 * x.value) + y.deriv1*((T) 2 * y.value)
			);
		}

		return result;
	}

	/// ============  Comparison & Assignment  ============
	void operator=(const DiffScalar& s) {
		env = s.env;
		value = s.value;
		deriv1 = s.deriv1;
		deriv2 = s.deriv2;
	}

	void operator=(const value_type &v) {
		value = v;
		deriv1.clear();
		deriv2.clear();
	}

	bool operator<(const DiffScalar& s) const {
		return value < s.value;
	}
	
	bool operator<=(const DiffScalar& s) const {
		return value <= s.value;
	}

	bool operator>(const DiffScalar& s) const {
		return value > s.value;
	}
	
	bool operator>=(const DiffScalar& s) const {
		return value >= s.value;
	}

	bool operator<(const value_type& s) const {
		return value < s;
	}
	
	bool operator<=(const value_type& s) const {
		return value <= s;
	}

	bool operator>(const value_type& s) const {
		return value > s;
	}
	
	bool operator>=(const value_type& s) const {
		return value >= s;
	}

	bool operator==(const value_type& s) const {
		return value == s;
	}

	bool operator!=(const value_type& s) const {
		return value != s;
	}

	inline const value_type &getValue() const { return value; }
	inline const deriv1_type &getGradient() const { return deriv1; }
	inline const deriv2_type &getHessian() const { return deriv2; }
	inline const DiffEnvBase<T> *getDiffEnv() const { return env; }
protected:
	const DiffEnvBase<T> *env;
	value_type value;
	deriv1_type deriv1;
	deriv2_type deriv2;
};

#endif AUTODIFF_H