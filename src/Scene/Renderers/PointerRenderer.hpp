#pragma once

#include "Renderer.hpp"

namespace stm {

class PointerRenderer : public Renderer {
public:
	inline PointerRenderer(const string& name, stm::Scene& scene) : Object(name, scene) {}
	
	inline virtual optional<fAABB> Bounds() override { ValidateTransform(); return mAABB; }
	inline virtual uint32_t RenderQueue(const string& pass) override { return 5000; }

	inline void RayDistance(float d) { mRayDistance = d; }
	inline void Color(const float4& c) { mColor = c; }
	inline void Width(float w) { mWidth = w; }

	inline float RayDistance() const { return mRayDistance; }
	inline float4 Color() const { return mColor; }
	inline float Width() const { return mWidth; }

protected:
	float mRayDistance = 0;
	float mWidth = 0.01f;
	float4 mColor = 1;

	fAABB mAABB;
	
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override;
	STRATUM_API virtual bool ValidateTransform() override;
};

}