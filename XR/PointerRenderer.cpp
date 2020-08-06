#include <Core/DescriptorSet.hpp>
#include <Data/AssetManager.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>
#include <XR/PointerRenderer.hpp>

using namespace std;

PointerRenderer::PointerRenderer(const string& name) : Object(name), mColor(1.f), mWidth(.01f), mRayDistance(1.f) {}
PointerRenderer::~PointerRenderer() {}

bool PointerRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB.mMin = min(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance));
	mAABB.mMax = max(WorldPosition(), WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance));
	return true;
}

void PointerRenderer::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	GraphicsPipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/pointer.stm")->GetGraphics(commandBuffer->CurrentShaderPass(), {});
	commandBuffer->BindPipeline(pipeline, nullptr, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	float3 p0 = WorldPosition();
	float3 p1 = WorldPosition() + WorldRotation() * float3(0, 0, mRayDistance);
	commandBuffer->PushConstantRef("P0", p0);
	commandBuffer->PushConstantRef("P1", p1);
	commandBuffer->PushConstantRef("Width", mWidth);
	commandBuffer->PushConstantRef("Color", mColor);

	camera->SetViewportScissor(commandBuffer, EYE_LEFT);
	vkCmdDraw(*commandBuffer, 6, 1, 0, 0);
	commandBuffer->mTriangleCount += 2;
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetViewportScissor(commandBuffer, EYE_RIGHT);
		vkCmdDraw(*commandBuffer, 6, 1, 0, 0);
		commandBuffer->mTriangleCount += 2;
	}
}