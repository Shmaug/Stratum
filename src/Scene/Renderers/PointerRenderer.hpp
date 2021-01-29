#pragma once

#include "Renderer.hpp"

namespace stm {

class PointerRenderer : public Renderer {
public:
	inline PointerRenderer(SceneNode& node, const string& name) : SceneNode::Component(node, name) {}
	
	inline virtual bool Visible() override { return mAABB; }
	inline virtual uint32_t RenderQueue(const string& pass) override { return 5000; }

	inline void RayDistance(float d) { mRayDistance = d; }
	inline void Color(const Vector4f& c) { mColor = c; }
	inline void Width(float w) { mWidth = w; }

	inline float RayDistance() const { return mRayDistance; }
	inline Vector4f Color() const { return mColor; }
	inline float Width() const { return mWidth; }

protected:
	float mRayDistance = 0;
	float mWidth = 0.01f;
	Vector4f mColor = Vector4f::Ones();

	AlignedBox3f mAABB;
	
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override;
	STRATUM_API virtual void OnValidateTransform(Matrix4f& globalTransform, TransformTraits& globalTransformTraits) override;
};

}