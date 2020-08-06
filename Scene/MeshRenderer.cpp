#include <Scene/MeshRenderer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>
#include <Util/Profiler.hpp>

#define INSTANCE_BATCH_SIZE 1024

using namespace std;

MeshRenderer::MeshRenderer(const string& name) : Object(name), mMesh(nullptr), mRayMask(0) {}
MeshRenderer::~MeshRenderer() {}

bool MeshRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	if (!Mesh())
		mAABB = AABB(WorldPosition(), WorldPosition());
	else
		mAABB = Mesh()->Bounds() * ObjectToWorld();
	return true;
}

void MeshRenderer::OnLateUpdate(CommandBuffer* commandBuffer) {
	mMaterial->OnLateUpdate(commandBuffer);
}

bool MeshRenderer::TryCombineInstances(CommandBuffer* commandBuffer, Renderer* renderer, Buffer*& instanceBuffer, uint32_t& instanceCount) {
	if (instanceCount + 1 >= INSTANCE_BATCH_SIZE) return false;

	MeshRenderer* mr = dynamic_cast<MeshRenderer*>(renderer);
	if (!mr || (mr->Material() != Material()) || mr->Mesh() != Mesh()) return false;

	// renderer is combinable
	if (!instanceBuffer) {
		instanceBuffer = commandBuffer->GetBuffer(mName + " batch", sizeof(InstanceBuffer) * INSTANCE_BATCH_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		instanceCount = 0;
	}
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->MappedData();
	buf[instanceCount].ObjectToWorld = ObjectToWorld();
	buf[instanceCount].WorldToObject = WorldToObject();
	instanceCount++;
	return true;
}

void MeshRenderer::OnDrawInstanced(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera, Buffer* instanceBuffer, uint32_t instanceCount) {
	::Mesh* mesh = Mesh();

	commandBuffer->BindMaterial(mMaterial.get(), mesh->VertexInput(), mesh->Topology());
	GraphicsPipeline* pipeline = mMaterial->GetPassPipeline(commandBuffer->CurrentShaderPass());

	if (pipeline->mShaderVariant->mDescriptorSetBindings.size() > PER_CAMERA)
		commandBuffer->BindDescriptorSet(perCamera, PER_CAMERA);
	
	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT] != VK_NULL_HANDLE) {
		DescriptorSet* perObject = commandBuffer->GetDescriptorSet(mName, pipeline->mDescriptorSetLayouts[PER_OBJECT]);
		perObject->CreateStorageBufferDescriptor(instanceBuffer, INSTANCE_BUFFER_BINDING);
		commandBuffer->BindDescriptorSet(perObject, PER_OBJECT);
	}

	Scene()->PushSceneConstants(commandBuffer);
	
	commandBuffer->BindVertexBuffer(mesh->VertexBuffer().get(), 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	
	camera->SetViewportScissor(commandBuffer, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
	commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetViewportScissor(commandBuffer, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
		commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	}
}

void MeshRenderer::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	Buffer* instanceBuffer = commandBuffer->GetBuffer(mName, sizeof(InstanceBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->MappedData();
	buf->ObjectToWorld = ObjectToWorld();
	buf->WorldToObject = WorldToObject();
	OnDrawInstanced(commandBuffer, camera, perCamera, instanceBuffer, 1);
}

bool MeshRenderer::Intersect(const Ray& ray, float* t, bool any) {
	::Mesh* m = Mesh();
	if (!m) return false;
	Ray r;
	r.mOrigin = (WorldToObject() * float4(ray.mOrigin, 1)).xyz;
	r.mDirection = (WorldToObject() * float4(ray.mDirection, 0)).xyz;
	return m->Intersect(r, t, any);
}