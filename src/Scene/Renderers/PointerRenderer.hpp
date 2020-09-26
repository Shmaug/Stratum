#pragma once

#include <Scene/Renderers/Renderer.hpp>

namespace stm {

class PointerRenderer : public Renderer {
public:
	inline PointerRenderer(const std::string& name, Scene* scene) : Object(name, scene) {}
	
	inline virtual std::optional<AABB> Bounds() override { UpdateTransform(); return mAABB; }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return 5000; }

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

	AABB mAABB;
	
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera, const std::shared_ptr<DescriptorSet>& perCamera) override;
	STRATUM_API virtual bool UpdateTransform() override;
};

}