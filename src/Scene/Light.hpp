#pragma once

#include "Object.hpp"

namespace stm {
#ifndef uint
typedef uint32_t uint;
#endif
#include "..\Shaders\include\lighting.hlsli"

enum class LightType : uint32_t {
	eDirectional = LIGHT_DISTANT,
	ePoint = LIGHT_SPHERE,
	eSpot = LIGHT_CONE,
};

class Light : public Object {
public:
	inline Light(const string& name, stm::Scene& scene) : Object(name, scene) {}

	inline void Color(const float3& c) { mColor = c; }
	inline float3 Color() const { return mColor; }

	inline void Intensity(float i) { mIntensity = i; }
	inline float Intensity() const { return mIntensity; }

	inline void Type(LightType t) { mType = t; }
	inline LightType Type() const { return mType; }

	inline void InnerSpotAngle(float a) { mInnerSpotAngle = a; }
	inline float InnerSpotAngle() const { return mInnerSpotAngle; }

	inline void OuterSpotAngle(float a) { mOuterSpotAngle = a; }
	inline float OuterSpotAngle() const { return mOuterSpotAngle; }

	// Distance light effects objects from
	inline void Range(float r) { mRange = r; }
	// Distance light effects objects from
	inline float Range() const { return mRange; }

	// Physical radius of the point/spot light
	inline void Radius(float r) { mRadius = r; }
	// Physical radius of the point/spot light
	inline float Radius() const { return mRadius; }

	inline void CastShadows(bool c) { mCastShadows = c; }
	inline bool CastShadows() { return mCastShadows; }

	inline void ShadowDistance(float d) { mShadowDistance = d; }
	inline float ShadowDistance() { return mShadowDistance; }

	inline void CascadeCount(uint32_t c) { mCascadeCount = c; }
	inline uint32_t CascadeCount() { return mCascadeCount; }
	
	inline optional<fAABB> Bounds() override {
		float3 c, e;
		switch (mType) {
		case LightType::ePoint:
			return fAABB(Position() - mRange, Position() + mRange);
		case LightType::eSpot:
			e = float3(0, 0, mRange * .5f);
			c = float3(mRange * float2(sinf(mOuterSpotAngle * .5f)), mRange * .5f);
			return fAABB(c - e, c + e) * Transform();
		case LightType::eDirectional:
			return fAABB(float3(-1e10f), float3(1e10f));
		}
		return {};
	}

private:
	float3 mColor = 1;
	float mIntensity = 1;
	LightType mType = LightType::ePoint;
	float mRange = 1;
	// Size of the physical point/spot light
	float mRadius = .01f;
	float mInnerSpotAngle = .34f;
	float mOuterSpotAngle = .35f;

	bool mCastShadows = false;
	uint32_t mCascadeCount = 2;
	float mShadowDistance = 256; // for directional lights
};

}