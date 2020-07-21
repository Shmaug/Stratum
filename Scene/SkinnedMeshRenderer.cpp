#include <Scene/SkinnedMeshRenderer.hpp>
#include <Scene/Scene.hpp>
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

void SkinnedMeshRenderer::PostUpdate(CommandBuffer* commandBuffer) {
	Shader* skinner = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/skinner.stm");
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

		ComputeShader* s = skinner->GetCompute("blend", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->mPipeline);
	
		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Blend", s->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(mVertexBuffer, s->mDescriptorBindings.at("Vertices").second.binding);
		ds->CreateStorageBufferDescriptor(targets[0], s->mDescriptorBindings.at("BlendTarget0").second.binding);
		ds->CreateStorageBufferDescriptor(targets[1], s->mDescriptorBindings.at("BlendTarget1").second.binding);
		ds->CreateStorageBufferDescriptor(targets[2], s->mDescriptorBindings.at("BlendTarget2").second.binding);
		ds->CreateStorageBufferDescriptor(targets[3], s->mDescriptorBindings.at("BlendTarget3").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->mPipelineLayout, 0, 1, *ds, 0, nullptr);

		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));
		commandBuffer->PushConstantRef("BlendFactors", weights);

		vkCmdDispatch(*commandBuffer, ( + 63) / 64, 1, 1);

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

		ComputeShader* s = skinner->GetCompute("skin", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->mPipeline);

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Skinning", s->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(mVertexBuffer, s->mDescriptorBindings.at("Vertices").second.binding);
		ds->CreateStorageBufferDescriptor(m->WeightBuffer().get(), s->mDescriptorBindings.at("Weights").second.binding);
		ds->CreateStorageBufferDescriptor(poseBuffer, s->mDescriptorBindings.at("Pose").second.binding);
		ds->FlushWrites();
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->mPipelineLayout, 0, 1, *ds, 0, nullptr);

		commandBuffer->PushConstantRef("VertexCount", m->VertexCount());
		commandBuffer->PushConstantRef("VertexStride", m->VertexSize());
		commandBuffer->PushConstantRef("NormalOffset", (uint32_t)offsetof(StdVertex, normal));
		commandBuffer->PushConstantRef("TangentOffset", (uint32_t)offsetof(StdVertex, tangent));

		vkCmdDispatch(*commandBuffer, (m->VertexCount() + 63) / 64, 1, 1);
	}

	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, barrier);
}

void SkinnedMeshRenderer::Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	::Mesh* mesh = this->Mesh();

	VkCullModeFlags cull = (pass == PASS_DEPTH) ? VK_CULL_MODE_NONE : VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
	VkPipelineLayout layout = commandBuffer->BindMaterial(mMaterial.get(), pass, mesh->VertexInput(), camera, mesh->Topology(), cull);
	if (!layout) return;
	GraphicsShader* shader = mMaterial->GetShader(pass);

	// TODO: Cache object descriptorset?

	Buffer* instanceBuffer = commandBuffer->GetBuffer(mName, sizeof(InstanceBuffer), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	InstanceBuffer* buf = (InstanceBuffer*)instanceBuffer->MappedData();
	buf->ObjectToWorld = ObjectToWorld();
	buf->WorldToObject = WorldToObject();
	DescriptorSet* objDS = commandBuffer->GetDescriptorSet(mName, shader->mDescriptorSetLayouts[PER_OBJECT]);
	objDS->CreateStorageBufferDescriptor(instanceBuffer, INSTANCE_BUFFER_BINDING, 0);
	objDS->FlushWrites();
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, PER_OBJECT, 1, *objDS, 0, nullptr);
	

	commandBuffer->BindVertexBuffer(mVertexBuffer, 0, 0);
	commandBuffer->BindIndexBuffer(mesh->IndexBuffer().get(), 0, mesh->IndexType());
	
	camera->SetStereoViewport(commandBuffer, EYE_LEFT);
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), mesh->BaseVertex(), 0);
	commandBuffer->mTriangleCount += mesh->IndexCount() / 3;

	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetStereoViewport(commandBuffer, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), mesh->BaseVertex(), 0);
		commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	}
}

void SkinnedMeshRenderer::DrawGui(CommandBuffer* commandBuffer, GuiContext* gui, Camera* camera) {
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