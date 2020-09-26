#include <Scene/Renderers/SkinnedMeshRenderer.hpp>

using namespace std;
using namespace stm;

void SkinnedMeshRenderer::Rig(const AnimationRig& rig) {
	mBoneMap.clear();
	mRig = rig;
	for (auto b : mRig) mBoneMap.emplace(b->mName, b);
}
Bone* SkinnedMeshRenderer::GetBone(const string& boneName) const {
	return mBoneMap.count(boneName) ? mBoneMap.at(boneName) : nullptr;
}

inline void SkinnedMeshRenderer::Mesh(shared_ptr<stm::Mesh> m) {
	mMesh = m;
	mSkinnedMesh = m.get(); // FIXME: make a copy of mMesh
}

void SkinnedMeshRenderer::OnLateUpdate(CommandBuffer& commandBuffer) {
	auto skinner = commandBuffer.mDevice->LoadAsset<Pipeline>("Assets/Shaders/skinner.stmb", "Skinner");
	
	// FIXME: fix this to work with new Mesh system

	// Shape Keys
	/*
	if (mShapeKeys.size()) {
		float4 weights = 0;
		Buffer* targets[4] {
			mVertexBuffer, mVertexBuffer, mVertexBuffer, mVertexBuffer
		};

		uint32_t ti = 0;

		for (auto& it : mShapeKeys) {
			if (it.second > -.0001f && it.second < .0001f) continue;
			auto k = m->ShapeKey(it.first);
			if (!k) continue;

			targets[ti] = k.get();
			weights[ti] = it.second;
			ti++;
			if (ti > 3) break;
		}

		ComputePipeline* s = skinner->GetCompute("blend", {});
	
		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Blend", s->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(mVertexBuffer, 0);
		ds->CreateStorageBufferDescriptor(targets[0], 1);
		ds->CreateStorageBufferDescriptor(targets[1], 2);
		ds->CreateStorageBufferDescriptor(targets[2], 3);
		ds->CreateStorageBufferDescriptor(targets[3], 4);
		commandBuffer->BindDescriptorSet(ds, 0);

		commandBuffer->BindPipeline(s);
		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));
		commandBuffer->PushConstantRef("BlendFactors", weights);
		commandBuffer->DispatchAligned(m->VertexCount());

		if (mRig.size()){
			barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
			commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, barrier);
		}
	}

	// Skeleton
	if (mRig.size()) {
		// bind space -> object space
		Buffer* poseBuffer = commandBuffer->GetBuffer(mName + " Pose", mRig.size() * sizeof(float4x4), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
		float4x4* skin = (float4x4*)poseBuffer->Mapped();
		for (uint32_t i = 0; i < mRig.size(); i++)
			skin[i] = (WorldToObject() * mRig[i]->ObjectToWorld()) * mRig[i]->mInverseBind; // * vertex;

		ComputePipeline* s = skinner->GetCompute("skin", {});
		commandBuffer->BindPipeline(s);

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Skinning", s->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(mVertexBuffer, 0);
		ds->CreateStorageBufferDescriptor(m->WeightBuffer().get(), 5);
		ds->CreateStorageBufferDescriptor(poseBuffer, 6);
		commandBuffer->BindDescriptorSet(ds, 0);

		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));

		commandBuffer->DispatchAligned(m->VertexCount());
	}

	barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexInput, barrier);
	*/
	mMaterial->OnLateUpdate(commandBuffer);
}

void SkinnedMeshRenderer::OnDraw(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<DescriptorSet>& perCamera) {
	GraphicsPipeline* pipeline = commandBuffer.BindPipeline(mMaterial, mSkinnedMesh);

	if (pipeline->mShaderVariant->mDescriptorSetBindings.size() > PER_CAMERA)
		commandBuffer.BindDescriptorSet(perCamera, PER_CAMERA);
	
	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT]) {
		// TODO: Cache object descriptorset?
		auto instanceBuffer = commandBuffer.GetBuffer(mName, sizeof(InstanceBuffer), vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->Mapped();
		buf->ObjectToWorld = ObjectToWorld();
		buf->WorldToObject = WorldToObject();
		auto perObject = commandBuffer.GetDescriptorSet(mName, pipeline->mDescriptorSetLayouts[PER_OBJECT]);
		perObject->CreateStorageBufferDescriptor(instanceBuffer, INSTANCES_BINDING, 0);
		commandBuffer.BindDescriptorSet(perObject, PER_OBJECT);
	}

	mSkinnedMesh->Draw(commandBuffer, &camera);
}

void SkinnedMeshRenderer::OnGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui) {
	if (mRig.size()){
		for (auto b : mRig) {
			gui.WireSphere(b->WorldPosition(), .01f, float4(0.25f, 1.f, 0.25f, 1.f));
			if (Bone* parent = dynamic_cast<Bone*>(b->Parent())) {
				float3 pts[2];
				pts[0] = b->WorldPosition();
				pts[1] = parent->WorldPosition();
				gui.PolyLine(float4x4(1), pts, 2, float4(0.25f, 1.f, 0.25f, 1.f), 1.5f);
			}
		}
	}
}