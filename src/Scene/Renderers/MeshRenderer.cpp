#include "MeshRenderer.hpp"

constexpr uint32_t INSTANCE_BATCH_SIZE = 1024;


using namespace stm;

bool MeshRenderer::ValidateTransform() {
	if (!Object::ValidateTransform()) return false;
	mAABB.reset();

	if (mMesh) {
		for (uint32_t i = 0; i < mMesh->SubmeshCount(); i++)
			if (mMesh->GetSubmesh(i).mBvh) {
				if (mAABB)
					mAABB->Encapsulate(mMesh->GetSubmesh(i).mBvh->GetNode(0).mBounds * Transform());
				else 
					mAABB = mMesh->GetSubmesh(i).mBvh->GetNode(0).mBounds * Transform();
			}
	}
	return true;
}

bool MeshRenderer::TryCombineInstances(CommandBuffer& commandBuffer, Renderer* renderer, shared_ptr<Buffer>& instanceBuffer, uint32_t& instanceCount) {
	if (instanceCount + 1 >= INSTANCE_BATCH_SIZE) return false;

	MeshRenderer* mr = dynamic_cast<MeshRenderer*>(renderer);
	if (!mr || (mr->Material() != Material()) || mr->Mesh() != Mesh()) return false;

	// renderer can be instanced with this one
	if (!instanceBuffer) {
		instanceBuffer = commandBuffer.GetBuffer(Name() + "/Instances", sizeof(InstanceData) * INSTANCE_BATCH_SIZE, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		instanceCount = 0;
	}
	InstanceData* buf = (InstanceData*)instanceBuffer->Mapped();
	buf[instanceCount].Transform = Transform();
	buf[instanceCount].InverseTransform = InverseTransform();
	instanceCount++;
	return true;
}

void MeshRenderer::OnDrawInstanced(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<Buffer>& instanceBuffer, uint32_t instanceCount) {
	mMaterial->Bind(commandBuffer, mMesh.get());
	commandBuffer.DrawMesh(*mMesh, instanceCount);
}
void MeshRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera) {
	auto instanceBuffer = commandBuffer.GetBuffer(Name()+"/Instances", sizeof(InstanceData), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
	InstanceData* buf = (InstanceData*)instanceBuffer->Mapped();
	buf->Transform = Transform();
	buf->InverseTransform = InverseTransform();
	OnDrawInstanced(commandBuffer, camera, instanceBuffer, 1);
}

bool MeshRenderer::Intersect(const fRay& ray, float* t, bool any) {
	if (!mMesh) return false;
	fRay r;
	r.mOrigin = (float3)(InverseTransform() * float4(ray.mOrigin, 1));
	r.mDirection = (float3)(InverseTransform() * float4(ray.mDirection, 0));
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