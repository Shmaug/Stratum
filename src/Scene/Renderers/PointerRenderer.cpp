#include <Scene/Renderers/PointerRenderer.hpp>

using namespace std;

PointerRenderer::PointerRenderer(const string& name) : Object(name), mColor(1.f), mWidth(.01f), mRayDistance(1.f) {}
PointerRenderer::~PointerRenderer() {}

bool PointerRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB.mMin = min(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance)) - mWidth;
	mAABB.mMax = max(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance)) + mWidth;
	return true;
}

void PointerRenderer::OnDraw(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera) {
	GraphicsPipeline* pipeline = commandBuffer->Device()->AssetManager()->Load<Pipeline>("Shaders/pointer.stmb", "Pointer")->GetGraphics(commandBuffer->CurrentShaderPass(), {});
	commandBuffer->BindPipeline(pipeline);

	float3 p0 = WorldPosition();
	float3 p1 = WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance);
	commandBuffer->PushConstantRef("P0", p0);
	commandBuffer->PushConstantRef("P1", p1);
	commandBuffer->PushConstantRef("Width", mWidth);
	commandBuffer->PushConstantRef("Color", mColor);

	camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
	((vk::CommandBuffer)*commandBuffer).draw(6, 1, 0, 0);
	commandBuffer->mTriangleCount += 2;
	if (camera->StereoMode() != StereoMode::eNone) {
		camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
		((vk::CommandBuffer)*commandBuffer).draw(6, 1, 0, 0);
		commandBuffer->mTriangleCount += 2;
	}
}