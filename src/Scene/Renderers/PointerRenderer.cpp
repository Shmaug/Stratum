#include "PointerRenderer.hpp"


using namespace stm;

bool PointerRenderer::ValidateTransform() {
	if (!Object::ValidateTransform()) return false;
	mAABB.mMin = min(Position(), Position() + Rotation() * float3(0, 0, mRayDistance)) - mWidth;
	mAABB.mMax = max(Position(), Position() + Rotation() * float3(0, 0, mRayDistance)) + mWidth;
	return true;
}

void PointerRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {	
	auto pipeline = mMaterial->Bind(commandBuffer);

	float3 p0 = Position();
	float3 p1 = Position() + Rotation() * float3(0, 0, mRayDistance);
	commandBuffer.PushConstantRef("P0", p0);
	commandBuffer.PushConstantRef("P1", p1);
	commandBuffer.PushConstantRef("Width", mWidth);
	commandBuffer.PushConstantRef("Color", mColor);

	camera.SetViewportScissor(commandBuffer, StereoEye::eLeft);
	commandBuffer->draw(6, 1, 0, 0);
	commandBuffer.mTriangleCount += 2;
	if (camera.StereoMode() != StereoMode::eNone) {
		camera.SetViewportScissor(commandBuffer, StereoEye::eRight);
		commandBuffer->draw(6, 1, 0, 0);
		commandBuffer.mTriangleCount += 2;
	}
}