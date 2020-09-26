#include <Scene/Renderers/PointerRenderer.hpp>

using namespace std;
using namespace stm;

bool PointerRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB.mMin = min(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance)) - mWidth;
	mAABB.mMax = max(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance)) + mWidth;
	return true;
}

void PointerRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<DescriptorSet>& perCamera) {
	GraphicsPipeline* pipeline = commandBuffer.mDevice->LoadAsset<Pipeline>("Assets/Shaders/pointer.stmb", "Pointer")->GetGraphics(commandBuffer.CurrentShaderPass(), {});
	commandBuffer.BindPipeline(pipeline);

	if (pipeline->mShaderVariant->mDescriptorSetBindings.size() > PER_CAMERA)
		commandBuffer.BindDescriptorSet(perCamera, PER_CAMERA);

	float3 p0 = WorldPosition();
	float3 p1 = WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance);
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