#include <Data/AssetManager.hpp>
#include <Scene/SkinnedMeshRenderer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Bone.hpp>
#include <Util/Profiler.hpp>

using namespace std;

SkinnedMeshRenderer::SkinnedMeshRenderer(const string& name) : Object(name) {}
SkinnedMeshRenderer::~SkinnedMeshRenderer() {}

bool SkinnedMeshRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	if (!Mesh())
		mAABB = AABB(WorldPosition(), WorldPosition());
	else
		mAABB = Mesh()->Bounds() * ObjectToWorld();
	return true;
}

void SkinnedMeshRenderer::Rig(const AnimationRig& rig) {
	mBoneMap.clear();
	mRig = rig;
	for (auto b : mRig) mBoneMap.emplace(b->mName, b);
}

Bone* SkinnedMeshRenderer::GetBone(const string& boneName) const {
	return mBoneMap.count(boneName) ? mBoneMap.at(boneName) : nullptr;
}

void SkinnedMeshRenderer::OnLateUpdate(CommandBuffer* commandBuffer) {
	Pipeline* skinner = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/skinner.stm");
	::Mesh* m = this->Mesh();

	mVertexBuffer = commandBuffer->GetBuffer(mName + " VertexBuffer", m->VertexBuffer()->Size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	VkBufferCopy rgn = {};
	rgn.size = mVertexBuffer->Size();
	vkCmdCopyBuffer(*commandBuffer, *m->VertexBuffer(), *mVertexBuffer, 1, &rgn);

	VkBufferMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.buffer = *mVertexBuffer;
	barrier.size = mVertexBuffer->Size();
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	commandBuffer->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barrier);

	// Shape Keys
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
		ds->CreateStorageBufferDescriptor(mVertexBuffer, s->GetDescriptorLocation("Vertices"));
		ds->CreateStorageBufferDescriptor(targets[0], s->GetDescriptorLocation("BlendTarget0"));
		ds->CreateStorageBufferDescriptor(targets[1], s->GetDescriptorLocation("BlendTarget1"));
		ds->CreateStorageBufferDescriptor(targets[2], s->GetDescriptorLocation("BlendTarget2"));
		ds->CreateStorageBufferDescriptor(targets[3], s->GetDescriptorLocation("BlendTarget3"));
		commandBuffer->BindDescriptorSet(ds, 0);

		commandBuffer->BindPipeline(s);
		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));
		commandBuffer->PushConstantRef("BlendFactors", weights);
		commandBuffer->DispatchAligned(m->VertexCount());

		if (mRig.size()){
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, barrier);
		}
	}

	// Skeleton
	if (mRig.size()) {
		// bind space -> object space
		Buffer* poseBuffer = commandBuffer->GetBuffer(mName + " Pose", mRig.size() * sizeof(float4x4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		float4x4* skin = (float4x4*)poseBuffer->MappedData();
		for (uint32_t i = 0; i < mRig.size(); i++)
			skin[i] = (WorldToObject() * mRig[i]->ObjectToWorld()) * mRig[i]->mInverseBind; // * vertex;

		ComputePipeline* s = skinner->GetCompute("skin", {});
		commandBuffer->BindPipeline(s);

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Skinning", s->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(mVertexBuffer, s->GetDescriptorLocation("Vertices"));
		ds->CreateStorageBufferDescriptor(m->WeightBuffer().get(), s->GetDescriptorLocation("Weights"));
		ds->CreateStorageBufferDescriptor(poseBuffer, s->GetDescriptorLocation("Pose"));
		commandBuffer->BindDescriptorSet(ds, 0);

		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));

		commandBuffer->DispatchAligned(m->VertexCount());
	}

	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, barrier);
	mMaterial->OnLateUpdate(commandBuffer);
}

void SkinnedMeshRenderer::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	::Mesh* mesh = this->Mesh();

	commandBuffer->BindMaterial(mMaterial.get(), mesh->VertexInput(), mesh->Topology());
	GraphicsPipeline* pipeline = mMaterial->GetPassPipeline(commandBuffer->CurrentShaderPass());

	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT] != VK_NULL_HANDLE) {
		// TODO: Cache object descriptorset?
		Buffer* instanceBuffer = commandBuffer->GetBuffer(mName, sizeof(InstanceBuffer), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->MappedData();
		buf->ObjectToWorld = ObjectToWorld();
		buf->WorldToObject = WorldToObject();
		DescriptorSet* perObject = commandBuffer->GetDescriptorSet(mName, pipeline->mDescriptorSetLayouts[PER_OBJECT]);
		perObject->CreateStorageBufferDescriptor(instanceBuffer, INSTANCE_BUFFER_BINDING, 0);
		commandBuffer->BindDescriptorSet(perObject, PER_OBJECT);
	}
	
	Scene()->PushSceneConstants(commandBuffer);
	
	commandBuffer->BindVertexBuffer(mVertexBuffer, 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	
	camera->SetViewportScissor(commandBuffer, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), mesh->BaseVertex(), 0);
	commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetViewportScissor(commandBuffer, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), mesh->BaseVertex(), 0);
		commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	}
}

void SkinnedMeshRenderer::OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) {
	if (mRig.size()){
		for (auto b : mRig) {
			gui->WireSphere(b->WorldPosition(), .01f, float4(0.25f, 1.f, 0.25f, 1.f));
			if (Bone* parent = dynamic_cast<Bone*>(b->Parent())) {
				float3 pts[2];
				pts[0] = b->WorldPosition();
				pts[1] = parent->WorldPosition();
				gui->PolyLine(float4x4(1), pts, 2, float4(0.25f, 1.f, 0.25f, 1.f), 1.5f);
			}
		}
	}
}