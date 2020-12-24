#pragma once

namespace stm {

template<typename T>
class AnimationCurve {
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

	struct Keyframe {
		float mTime;
		T mValue;
		T mTangentIn;
		T mTangentOut;
		TangentMode mTangentModeIn = TangentMode::eSmooth;
		TangentMode mTangentModeOut = TangentMode::eSmooth;
	};

	AnimationCurve() = default;
	inline AnimationCurve(const vector<Keyframe>& keyframes, ExtrapolateMode in = ExtrapolateMode::eConstant, ExtrapolateMode out = ExtrapolateMode::eConstant)
		: mKeyframes(keyframes), mExtrapolateIn(in), mExtrapolateOut(out) {

		if (!mKeyframes.size()) return;
		
		// compute tangents
		for (uint32_t i = 0; i < mKeyframes.size(); i++) {
			switch (mKeyframes[i].mTangentModeIn) {
			case AnimationTangentMode::eSmooth:
				if (i > 0 && i < mKeyframes.size()-1) {
					mKeyframes[i].mTangentIn = (mKeyframes[i + 1].mValue - mKeyframes[i - 1].mValue) / (mKeyframes[i + 1].mTime - mKeyframes[i - 1].mTime);
					break;
				}
			case AnimationTangentMode::eLinear:
				if (i > 0) mKeyframes[i].mTangentIn = (mKeyframes[i].mValue - mKeyframes[i-1].mValue) / (mKeyframes[i].mTime - mKeyframes[i - 1].mTime);
				break;
			}

			switch (mKeyframes[i].mTangentModeOut) {
			case AnimationTangentMode::eSmooth:
				if (i > 0 && i < mKeyframes.size() - 1) {
					mKeyframes[i].mTangentOut = (mKeyframes[i + 1].mValue - mKeyframes[i - 1].mValue) / (mKeyframes[i + 1].mTime - mKeyframes[i - 1].mTime);
					break;
				}
			case AnimationTangentMode::eLinear:
				if (i < mKeyframes.size() - 1) mKeyframes[i].mTangentOut = (mKeyframes[i + 1].mValue - mKeyframes[i].mValue) / (mKeyframes[i + 1].mTime - mKeyframes[i].mTime);
				break;
			}
		}

		mKeyframes[0].mTangentIn = mKeyframes[0].mTangentOut;
		mKeyframes[mKeyframes.size() - 1].mTangentOut = mKeyframes[mKeyframes.size() - 1].mTangentIn;
		
		// compute curve
		mCoefficients.resize(mKeyframes.size());
		memset(mCoefficients.data(), 0, sizeof(float4) * mCoefficients.size());
		for (uint32_t i = 0; i < mKeyframes.size() - 1; i++) {

			float ts =  mKeyframes[i + 1].mTime - mKeyframes[i].mTime;

			T p0y = mKeyframes[i].mValue;
			T p1y = mKeyframes[i + 1].mValue;
			T v0 = mKeyframes[i].mTangentOut * ts;
			T v1 = mKeyframes[i + 1].mTangentIn * ts;

			mCoefficients[i].d = p0y;
			if (mKeyframes[i].mTangentModeOut == AnimationTangentMode::eStep) continue;

			mCoefficients[i].c = v0;
			mCoefficients[i].b = 3 * (p1y - p0y) - 2*v0 - v1;
			mCoefficients[i].a = p1y - p0y - v0 - mCoefficients[i].b;
		}
	}

	inline T Sample(float t) const {
		if (mKeyframes.size() == 0) return 0;
		if (mKeyframes.size() == 1) return mKeyframes[0].mValue;

		const Keyframe& first = mKeyframes[0];
		const Keyframe& last = mKeyframes[mKeyframes.size() - 1];

		float length = last.mTime - first.mTime;
		float ts = first.mTime - t;
		float tl = t - last.mTime;
		T offset = 0;
		
		if (tl > 0) {
			switch (mExtrapolateOut) {
			case AnimationExtrapolateMode::eConstant:
				return last.mValue;
			case AnimationExtrapolateMode::eLinear:
				return last.mValue + last.mTangentOut * tl;
			case AnimationExtrapolateMode::eCycleOffset:
				offset += (last.mValue - first.mValue) * (floorf(tl / length) + 1);
			case AnimationExtrapolateMode::eCycle:
				t = fmodf(tl, length);
				break;
			case AnimationExtrapolateMode::eBounce:
				t = fmodf(tl, 2 * length);
				if (t > length) t = 2 * length - t;
				break;
			}
			t += first.mTime;
		}
		if (ts > 0) {
			switch (mExtrapolateIn) {
			case AnimationExtrapolateMode::eConstant:
				return first.mValue;
			case AnimationExtrapolateMode::eLinear:
				return first.mValue - first.mTangentIn * ts;
			case AnimationExtrapolateMode::eCycleOffset:
				offset += (first.mValue - last.mValue) * (floorf(ts / length) + 1);
			case AnimationExtrapolateMode::eCycle:
				t = fmodf(ts, length);
				break;
			case AnimationExtrapolateMode::eBounce:
				t = fmodf(ts, 2 * length);
				if (t > length) t = 2 * length - t;
				break;
			}
			t = last.mTime - t; // looping anims loop back to last key
		}

		// find the first key after t
		uint32_t i = 0;
		for (uint32_t j = 1; j < (uint32_t)mKeyframes.size(); j++)
			if (mKeyframes[j].mTime > t) {
				i = j - 1;
				break;
			}

		float u = (t - mKeyframes[i].mTime) / (mKeyframes[i + 1].mTime - mKeyframes[i].mTime);
		Coeff c = mCoefficients[i];
		return u*u*u*c.a + u*u*c.b + u*c.c + c.d + offset;
	}

	inline ExtrapolateMode ExtrapolateIn() const { return mExtrapolateIn; }
	inline ExtrapolateMode ExtrapolateOut() const { return mExtrapolateOut; }
	inline uint32_t KeyframeCount() const { return (uint32_t)mKeyframes.size(); }
	inline Keyframe Keyframe(uint32_t index) const { return mKeyframes[index]; }
	inline float4 CurveCoefficient(uint32_t index) const { return mCoefficients[index]; }

	inline Keyframe& operator[](uint32_t index) {
		return mKeyframes[index];
	}

private:
	struct Coeff { T a, b, c, d; };

	ExtrapolateMode mExtrapolateIn  = ExtrapolateMode::eConstant;
	ExtrapolateMode mExtrapolateOut = ExtrapolateMode::eConstant;
	vector<Coeff> mCoefficients;
	vector<Keyframe> mKeyframes;
};

}