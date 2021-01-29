#include "PointerRenderer.hpp"


using namespace stm;

void PointerRenderer::OnValidateTransform(Matrix4f& globalTransform, TransformTraits& globalTransformTraits) {
	auto t = globalTransform.translation();
	auto r = globalTransform.rotation();
	mAABB.min() = min(t, t + r*Vector3f(0, 0, mRayDistance)) - Vector3f::Constant(mWidth);
	mAABB.max() = max(t, t + r*Vector3f(0, 0, mRayDistance)) + Vector3f::Constant(mWidth);
}

void PointerRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {	
	auto pipeline = Material()->Bind(commandBuffer);

	Vector3f p0 = mNode.Translation();
	Vector3f p1 = mNode.Translation() + mNode.Rotation() * Vector3f(0, 0, mRayDistance);
	commandBuffer.PushConstantRef("P0", p0);
	commandBuffer.PushConstantRef("P1", p1);
	commandBuffer.PushConstantRef("Width", mWidth);
	commandBuffer.PushConstantRef("Color", mColor);

	camera.SetViewportScissor(commandBuffer);
	commandBuffer->draw(6, 1, 0, 0);
	commandBuffer.mTriangleCount += 2;
}