#pragma once

#include "common.hpp"

namespace stm {

template<typename T> requires(is_arithmetic_v<T>)
class Spline {
public:
	enum class ExtrapolateMode {
		eConstant,
		eLinear,
		eCycle,
		eCycleOffset,
		eBounce,
	};
	enum class TangentMode {
		eManual,
		eFlat,
		eLinear,
		eSmooth,
		eStep,
	};

	struct Key {
		chrono::milliseconds mTime;
		T mValue;
		T mTangentIn;
		T mTangentOut;
		TangentMode mTangentModeIn = TangentMode::eSmooth;
		TangentMode mTangentModeOut = TangentMode::eSmooth;
	};

	Spline() = default;
	Spline(const Spline&) = default;
	Spline(Spline&&) = default;
	template<range_of<Key> R>
	inline Spline(R&& keyframes, ExtrapolateMode in = ExtrapolateMode::eConstant, ExtrapolateMode out = ExtrapolateMode::eConstant)
		: mExtrapolateIn(in), mExtrapolateOut(out) {
		ranges::copy(keyframes, back_inserter(mKeys));
		if (mKeys.empty()) return;
		
		// compute tangents
		for (auto k = mKeys.begin(); k != mKeys.end(); k++) {
			switch (k->mTangentModeIn) {
			case TangentMode::eSmooth:
				if (i > 0 && i < mKeys.size()-1) {
					k->mTangentIn = (next(k)->mValue - mKeys[i - 1].mValue) / (next(k)->mTime - mKeys[i - 1].mTime);
					break;
				}
			case TangentMode::eLinear:
				if (i > 0) k->mTangentIn = (k->mValue - mKeys[i - 1].mValue) / (k->mTime - mKeys[i - 1].mTime);
				break;
			}
			switch (k->mTangentModeOut) {
			case TangentMode::eSmooth:
				if (i > 0 && i < mKeys.size() - 1) {
					k->mTangentOut = (next(k)->mValue - mKeys[i - 1].mValue) / (next(k)->mTime - mKeys[i - 1].mTime);
					break;
				}
			case TangentMode::eLinear:
				if (i < mKeys.size() - 1) k->mTangentOut = (next(k)->mValue - k->mValue) / (next(k)->mTime - k->mTime);
				break;
			}
		}

		mKeys.front().mTangentIn = mKeys.front()mTangentOut;
		mKeys.back().mTangentOut = mKeys.back().mTangentIn;
		
		// compute coefficients
		mCoefficients.resize(mKeys.size());
		for (uint32_t i = 0; i < mKeys.size() - 1; i++) {
			const Key& k0 = mKeys[i];
			const Key& k1 = mKeys[i+1];
			Eigen::Array<T,4>& c = mCoefficients[i];

			auto ts = chrono::duration_cast<chrono::duration<float, chrono::seconds>>(k1.mTime - k0.mTime);

			c[0] = k0.mValue;
			if (k.mTangentModeOut != TangentMode::eStep) {
				c[1] = k0.mTangentOut * ts;
				c[2] = 3 * (k1.mValue - c[0]) - 2*c[1] - k1.mTangentIn * ts;
				c[3] = k1.mValue - c[0] - c[1] - c[2];
			}
		}
	}

	inline T sample(float t) const {
		if (mKeys.empty()) return {};

		const float length = mKeys.back().mTime - mKeys.front().mTime;
		const float ts = mKeys.front().mTime - t;
		const float tl = t - mKeys.back().mTime;
		T offset = 0;
		
		if (tl > 0) {
			switch (mExtrapolateOut) {
			case ExtrapolateMode::eConstant:
				return mKeys.back().mValue;
			case ExtrapolateMode::eLinear:
				return mKeys.back().mValue + mKeys.back().mTangentOut * tl;
			case ExtrapolateMode::eCycleOffset:
				offset += (mKeys.back().mValue - mKeys.front().mValue) * (floorf(tl / length) + 1);
			case ExtrapolateMode::eCycle:
				t = fmodf(tl, length);
				break;
			case ExtrapolateMode::eBounce:
				t = fmodf(tl, 2 * length);
				if (t > length) t = 2 * length - t;
				break;
			}
			t += mKeys.front().mTime;
		}
		if (ts > 0) {
			switch (mExtrapolateIn) {
			case ExtrapolateMode::eConstant:
				return mKeys.front().mValue;
			case ExtrapolateMode::eLinear:
				return mKeys.front().mValue - mKeys.front().mTangentIn * ts;
			case ExtrapolateMode::eCycleOffset:
				offset += (mKeys.front().mValue - mKeys.back().mValue) * (floorf(ts / length) + 1);
			case ExtrapolateMode::eCycle:
				t = fmodf(ts, length);
				break;
			case ExtrapolateMode::eBounce:
				t = fmodf(ts, 2 * length);
				if (t > length) t = 2 * length - t;
				break;
			}
			t = mKeys.back().mTime - t; // looping anims loop back to last key
		}

		// find the first key after t
		uint32_t i;
		for (i = 0; i < (uint32_t)mKeys.size() - 1; i++)
			if (mKeys[i + 1]->mTime > t)
				break;
			
		const Key& k = mKeyframes[i];
		const Eigen::Array<T,4>& c = mCoefficients[i];
		float u = (t - k.mTime) / (k.mTime - k.mTime);
		float u2 = u*u;
		return c.dot(Eigen::Array<T,4>(1, u, u2, u2*u)) + offset;
		//return u2*u*c[3] + u2*c[2] + u*c[1] + c[0] + offset;
	}

	inline ExtrapolateMode extrapolate_in() const { return mExtrapolateIn; }
	inline ExtrapolateMode extrapolate_out() const { return mExtrapolateOut; }
	inline Key at(size_t index) const { return mKeys[index]; }
	inline Eigen::Array<T,4> coefficient(size_t index) const { return mCoefficients[index]; }

	inline vector<Key>::iterator begin() { return mKeys.begin(); }
	inline vector<Key>::iterator end() { return mKeys.end(); }
	inline vector<Key>::const_iterator begin() const { return mKeys.begin(); }
	inline vector<Key>::const_iterator end() const { return mKeys.end(); }

	inline Key& operator[](size_t index) { return mKeys[index]; }

private:
	ExtrapolateMode mExtrapolateIn  = ExtrapolateMode::eConstant;
	ExtrapolateMode mExtrapolateOut = ExtrapolateMode::eConstant;
	vector<Eigen::Array<T,4>> mCoefficients;
	vector<Key> mKeys;
};

}