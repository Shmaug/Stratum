#pragma once

#include <Scene/Object.hpp>

class Bone : public virtual Object {
public:
	uint32_t mBoneIndex;
	float4x4 mInverseBind;
	inline Bone(const std::string& name, uint32_t index) : Object(name), mInverseBind(float4x4(1)), mBoneIndex(index) {}
};