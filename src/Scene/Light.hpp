#pragma once

#include "Scene.hpp"

namespace stm {
namespace shader_interop {
	#include "..\Shaders\include\lighting.hlsli"
}

enum class LightType : uint32_t {
	eDirectional = LIGHT_DISTANT,
	ePoint = LIGHT_SPHERE,
	eSpot = LIGHT_CONE,
};

class Light : public SceneNode::Component {
public:
	inline Light(SceneNode& node, const string& name) : Component(node, name) {}

	inline void Color(const Vector3f& c) { mColor = c; }
	inline Vector3f Color() const { return mColor; }

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
	
	inline AlignedBox3f Bounds() {
		Vector3f c, e;
		switch (mType) {
		case LightType::ePoint:
			return AlignedBox3f(mNode.Translation() - Vector3f::Constant(mRange), mNode.Translation() + Vector3f::Constant(mRange));
		case LightType::eSpot:
			e = Vector3f(0, 0, mRange * .5f);
			c = Vector3f(mRange * Vector2f(sinf(mOuterSpotAngle * .5f)), mRange * .5f);
			return AlignedBox3f(c - e, c + e) * Transform();
		case LightType::eDirectional:
			return AlignedBox3f(Vector3f::Constant(-1e24f), Vector3f::Constant(1e124));
		}
		return mNode.Bounds();
	}

private:
	Vector3f mColor = Vector3f::Ones();
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