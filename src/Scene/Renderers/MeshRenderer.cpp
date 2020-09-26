#include <Scene/Renderers/MeshRenderer.hpp>

constexpr uint32_t INSTANCE_BATCH_SIZE = 1024;

using namespace std;
using namespace stm;

bool MeshRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	mAABB.reset();

	if (mMesh) {
		for (uint32_t i = 0; i < mMesh->SubmeshCount(); i++)
			if (mMesh->GetSubmesh(i).mBvh) {
				if (mAABB)
					mAABB->Encapsulate(mMesh->GetSubmesh(i).mBvh->Bounds() * ObjectToWorld());
				else 
					mAABB = mMesh->GetSubmesh(i).mBvh->Bounds() * ObjectToWorld();
			}
	}
	return true;
}

void MeshRenderer::OnLateUpdate(CommandBuffer& commandBuffer) {
	mMaterial->OnLateUpdate(commandBuffer);
}

bool MeshRenderer::TryCombineInstances(CommandBuffer& commandBuffer, Renderer* renderer, shared_ptr<Buffer>& instanceBuffer, uint32_t& instanceCount) {
	if (instanceCount + 1 >= INSTANCE_BATCH_SIZE) return false;

	MeshRenderer* mr = dynamic_cast<MeshRenderer*>(renderer);
	if (!mr || (mr->Material() != Material()) || mr->Mesh() != Mesh()) return false;

	// renderer can be instanced with this one
	if (!instanceBuffer) {
		instanceBuffer = commandBuffer.GetBuffer(mName + "/Instances", sizeof(InstanceBuffer) * INSTANCE_BATCH_SIZE, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		instanceCount = 0;
	}
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->Mapped();
	buf[instanceCount].ObjectToWorld = ObjectToWorld();
	buf[instanceCount].WorldToObject = WorldToObject();
	instanceCount++;
	return true;
}

void MeshRenderer::OnDrawInstanced(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<DescriptorSet>& perCamera, const shared_ptr<Buffer>& instanceBuffer, uint32_t instanceCount) {
	GraphicsPipeline* pipeline = commandBuffer.BindPipeline(mMaterial, mMesh.get());
	
	if (pipeline->mShaderVariant->mDescriptorSetBindings.size() > PER_CAMERA)
		commandBuffer.BindDescriptorSet(perCamera, PER_CAMERA);
	
	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT]) {
		auto perObject = commandBuffer.GetDescriptorSet(mName, pipeline->mDescriptorSetLayouts[PER_OBJECT]);
		perObject->CreateStorageBufferDescriptor(instanceBuffer, INSTANCES_BINDING);
		commandBuffer.BindDescriptorSet(perObject, PER_OBJECT);
	}
	
	mMesh->Draw(commandBuffer, &camera, instanceCount);
}
void MeshRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<DescriptorSet>& perCamera) {
	auto instanceBuffer = commandBuffer.GetBuffer(mName + "/Instances", sizeof(InstanceBuffer), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->Mapped();
	buf->ObjectToWorld = ObjectToWorld();
	buf->WorldToObject = WorldToObject();
	OnDrawInstanced(commandBuffer, camera, perCamera, instanceBuffer, 1);
}

bool MeshRenderer::Intersect(const Ray& ray, float* t, bool any) {
	if (!mMesh) return false;
	Ray r;
	r.mOrigin = (WorldToObject() * float4(ray.mOrigin, 1)).xyz;
	r.mDirection = (WorldToObject() * float4(ray.mDirection, 0)).xyz;
	bool hit = false;
	float tmin = 0;
	for (uint32_t i = 0; i < mMesh->SubmeshCount(); i++) {
		float ht;
		if (mMesh->GetSubmesh(i).mBvh && mMesh->GetSubmesh(i).mBvh->Intersect(r, &ht, any)) {
			if (any) return true;
			if (!hit || ht < tmin) tmin = ht;
			hit = true;
		}
	}
	return hit;
}