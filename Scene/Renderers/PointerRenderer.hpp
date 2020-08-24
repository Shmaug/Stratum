#pragma once

#include <Scene/Renderers/Renderer.hpp>

class PointerRenderer : public Renderer {
public:
	STRATUM_API PointerRenderer(const std::string& name);
	STRATUM_API ~PointerRenderer();
	
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return 5000; }
	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }

	inline void RayDistance(float d) { mRayDistance = d; }
	inline void Color(const float4& c) { mColor = c; }
	inline void Width(float w) { mWidth = w; }

	inline float RayDistance() const { return mRayDistance; }
	inline float4 Color() const { return mColor; }
	inline float Width() const { return mWidth; }

protected:
	float mRayDistance;
	float mWidth;
	float4 mColor;

	AABB mAABB;
	
	STRATUM_API virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) override;
	STRATUM_API virtual bool UpdateTransform() override;
};