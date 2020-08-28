#pragma once

#include <Scene/Object.hpp>

class Bone : public virtual Object {
public:
	uint32_t mBoneIndex;
	float4x4 mInverseBind;
	inline Bone(const std::string& name, uint32_t index) : Object(name), mInverseBind(float4x4(1)), mBoneIndex(index) {}
};

struct BoneTransform {
	float3 mPosition;
	quaternion mRotation;
	float3 mScale;

	inline BoneTransform operator*(const BoneTransform& rhs) const {
		BoneTransform t = {};
		t.mPosition = mPosition + (mRotation * rhs.mPosition) * mScale;
		t.mRotation = mRotation * rhs.mRotation;
		t.mScale = rhs.mScale * mScale;
		return t;
	}
};

typedef std::vector<BoneTransform> Pose;

inline BoneTransform inverse(const BoneTransform& bt) {
	BoneTransform t = {};
	t.mRotation = inverse(bt.mRotation);
	t.mPosition = (t.mRotation * -bt.mPosition) / bt.mScale;
	t.mScale = 1.f / bt.mScale;
	return t;
}
inline BoneTransform lerp(const BoneTransform& p0, const BoneTransform& p1, float t) {
	BoneTransform dest;
	dest.mPosition = lerp(p0.mPosition, p1.mPosition, t);
	dest.mRotation = slerp(p0.mRotation, p1.mRotation, t);
	dest.mScale = lerp(p0.mScale, p1.mScale, t);
	return dest;
}
inline void lerp(Pose& dest, const Pose& p0, const Pose& p1, float t) {
	for (uint32_t i = 0; i < dest.size(); i++)
		dest[i] = lerp(p0[i], p1[i], t);
}