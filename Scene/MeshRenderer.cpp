#include <Scene/MeshRenderer.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>

#include <Shaders/include/shadercompat.h>

#define INSTANCE_BATCH_SIZE 1024

using namespace std;

MeshRenderer::MeshRenderer(const string& name)
	: Object(name), mVisible(true), mMesh(nullptr), mRayMask(0) {}
MeshRenderer::~MeshRenderer() {}

bool MeshRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	if (!Mesh())
		mAABB = AABB(WorldPosition(), WorldPosition());
	else
		mAABB = Mesh()->Bounds() * ObjectToWorld();
	return true;
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

void MeshRenderer::PreBeginRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
		Scene()->SetEnvironmentParameters(mMaterial.get());
		mMaterial->PreBeginRenderPass(commandBuffer, pass);
}

void MeshRenderer::DrawInstanced(CommandBuffer* commandBuffer, Camera* camera, PassType pass, Buffer* instanceBuffer, uint32_t instanceCount) {
	::Mesh* mesh = Mesh();

	VkCullModeFlags cull = (pass == PASS_DEPTH) ? VK_CULL_MODE_NONE : VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
	VkPipelineLayout layout = commandBuffer->BindMaterial(mMaterial.get(), pass, mesh->VertexInput(), camera, mesh->Topology(), cull);
	if (!layout) return;
	auto shader = mMaterial->GetShader(pass);
	
	// TODO: Cache object descriptorset?
	
	DescriptorSet* objDS = commandBuffer->GetDescriptorSet(mName, shader->mDescriptorSetLayouts[PER_OBJECT]);
	objDS->CreateStorageBufferDescriptor(instanceBuffer, INSTANCE_BUFFER_BINDING, 0);
	objDS->FlushWrites();
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *objDS, 0, nullptr);

	
	commandBuffer->BindVertexBuffer(mesh->VertexBuffer().get(), 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	
	camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
	commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), instanceCount, mesh->BaseIndex(), mesh->BaseVertex(), 0);
		commandBuffer->mTriangleCount += instanceCount * (mesh->IndexCount() / 3);
	}
}

void MeshRenderer::Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	Buffer* instanceBuffer = commandBuffer->GetBuffer(mName, sizeof(InstanceBuffer), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->MappedData();
	buf->ObjectToWorld = ObjectToWorld();
	buf->WorldToObject = WorldToObject();
	DrawInstanced(commandBuffer, camera, pass, instanceBuffer, 1);
}

bool MeshRenderer::Intersect(const Ray& ray, float* t, bool any) {
	::Mesh* m = Mesh();
	if (!m) return false;
	Ray r;
	r.mOrigin = (WorldToObject() * float4(ray.mOrigin, 1)).xyz;
	r.mDirection = (WorldToObject() * float4(ray.mDirection, 0)).xyz;
	return m->Intersect(r, t, any);
}