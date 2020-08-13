#include <Data/AssetManager.hpp>
#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>

#include "ClothRenderer.hpp"

using namespace std;

ClothRenderer::ClothRenderer(const string& name)
	:  Object(name), mMove(0),
	mVertexBuffer(nullptr), mVelocityBuffer(nullptr), mForceBuffer(nullptr), mEdgeBuffer(nullptr), mCopyVertices(false), mPin(true),
	mFriction(5), mDrag(1), mStiffness(1000), mDamping(0.5f), mGravity(float3(0,-9.8f,0)) {}
ClothRenderer::~ClothRenderer() { safe_delete(mVertexBuffer); safe_delete(mVelocityBuffer); safe_delete(mForceBuffer); safe_delete(mEdgeBuffer); }

void ClothRenderer::Mesh(::Mesh* m) {
	mMesh = m;
	DirtyTransform();

	// safe_delete(mVertexBuffer);
	// safe_delete(mVelocityBuffer);
	// safe_delete(mForceBuffer);
	// safe_delete(mEdgeBuffer);
	if (m) {
		uint32_t x = m->VertexCount()-1;

		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mVelocityBuffer = new Buffer(mName + "Velocities", m->VertexBuffer()->Device(), m->VertexCount() * sizeof(float4), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mEdgeBuffer = new Buffer(mName + "Edges", m->VertexBuffer()->Device(), (((x+x)*(x+x+1)/2+x)+31)/32, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mCopyVertices = true;
	}
}
void ClothRenderer::Mesh(std::shared_ptr<::Mesh> m) {
	mMesh = m;
	DirtyTransform();

	// safe_delete(mVertexBuffer);
	// safe_delete(mVelocityBuffer);
	// safe_delete(mForceBuffer);
	// safe_delete(mEdgeBuffer);
	if (m) {
		uint32_t x = m->VertexCount()-1;

		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mVelocityBuffer = new Buffer(mName + "Velocities", m->VertexBuffer()->Device(), m->VertexCount() * sizeof(float4), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mEdgeBuffer = new Buffer(mName + "Edges", m->VertexBuffer()->Device(), (((x+x)*(x+x+1)/2+x)+31)/32, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mCopyVertices = true;
	}
}

bool ClothRenderer::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;
	if (!Mesh())
		mAABB = AABB(WorldPosition(), WorldPosition());
	else
		mAABB = Mesh()->Bounds() * ObjectToWorld();
	float3 e = mAABB.HalfSize();
	mAABB.mMin -= 10*e;
	mAABB.mMax += 10*e;
	mAABB *= ObjectToWorld();
	return true;
}

void ClothRenderer::OnFixedUpdate(CommandBuffer* commandBuffer) {
	if (!mVertexBuffer) return;
	::Mesh* m = this->Mesh();

	VkBufferMemoryBarrier b = {};
	b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

	if (mCopyVertices) {
		VkBufferCopy cpy = {};
		cpy.srcOffset = m->VertexSize() * m->BaseVertex();
		cpy.size = mVertexBuffer->Size();
		vkCmdCopyBuffer(*commandBuffer, *m->VertexBuffer(), *mVertexBuffer, 1, &cpy);
		vkCmdFillBuffer(*commandBuffer, *mVelocityBuffer, 0, mVelocityBuffer->Size(), 0);

		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		b.buffer = *mVertexBuffer;
		b.size = mVertexBuffer->Size();
		commandBuffer->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);

		b.buffer = *mVelocityBuffer;
		b.size = mVelocityBuffer->Size();
		commandBuffer->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);

		mCopyVertices = false;
	}

	vkCmdFillBuffer(*commandBuffer, *mForceBuffer, 0, mForceBuffer->Size(), 0);
	vkCmdFillBuffer(*commandBuffer, *mEdgeBuffer, 0, mEdgeBuffer->Size(), 0);
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);
	b.buffer = *mEdgeBuffer;
	b.size = mEdgeBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);

	Pipeline* shader = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/cloth.stmb");

	Buffer* sphereBuffer = commandBuffer->GetBuffer("Cloth Spheres", sizeof(float4) * max(1u, (uint32_t)mSphereColliders.size()), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	for (uint32_t i = 0; i < mSphereColliders.size(); i++)
		((float4*)sphereBuffer->MappedData())[i] = float4(mSphereColliders[i].first->WorldPosition(), mSphereColliders[i].second);
	
	Buffer* objBuffer = commandBuffer->GetBuffer("Cloth Obj", sizeof(float4x4) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	((float4x4*)objBuffer->MappedData())[0] = ObjectToWorld();
	((float4x4*)objBuffer->MappedData())[1] = WorldToObject();

	uint32_t triCount = m->IndexCount()/3;
	uint32_t vs = sizeof(StdVertex);

	VkDeviceSize baseVertex = m->BaseVertex() * m->VertexSize();
	VkDeviceSize baseIndex = m->BaseIndex() * (m->IndexType() == VK_INDEX_TYPE_UINT16 ? sizeof(uint16_t) : sizeof(uint32_t));

	ComputePipeline* add = m->IndexType() == VK_INDEX_TYPE_UINT16 ? shader->GetCompute("AddForces", {}) : shader->GetCompute("AddForces", {"INDEX_UINT32"});
	DescriptorSet* ds = commandBuffer->GetDescriptorSet("AddForces", add->mDescriptorSetLayouts[0]);
	ds->CreateUniformBufferDescriptor(objBuffer, add->GetDescriptorLocation("ObjectBuffer"));
	ds->CreateStorageBufferDescriptor(m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, add->GetDescriptorLocation("SourceVertices"));
	ds->CreateStorageBufferDescriptor(m->IndexBuffer().get(), baseIndex, m->IndexBuffer()->Size() - baseIndex, add->GetDescriptorLocation("Triangles"));
	ds->CreateStorageBufferDescriptor(mVertexBuffer, add->GetDescriptorLocation("Vertices"));
	ds->CreateStorageBufferDescriptor(mVelocityBuffer, add->GetDescriptorLocation("Velocities"));
	ds->CreateStorageBufferDescriptor(mForceBuffer, add->GetDescriptorLocation("Forces"));
	ds->CreateStorageBufferDescriptor(mEdgeBuffer, add->GetDescriptorLocation("Edges"));
	commandBuffer->BindDescriptorSet(ds, 0);

	commandBuffer->BindPipeline(add);
	commandBuffer->PushConstantRef<uint32_t>("TriangleCount", triCount);
	commandBuffer->PushConstantRef<uint32_t>("VertexCount", m->VertexCount());
	commandBuffer->PushConstantRef<uint32_t>("VertexSize", m->VertexSize());
	commandBuffer->PushConstantRef<uint32_t>("NormalLocation", (uint32_t)offsetof(StdVertex, normal));
	commandBuffer->PushConstantRef<uint32_t>("TangentLocation", (uint32_t)offsetof(StdVertex, tangent));
	commandBuffer->PushConstantRef<uint32_t>("TexcoordLocation", (uint32_t)offsetof(StdVertex, uv));
	commandBuffer->PushConstantRef<float>("Friction", mFriction);
	commandBuffer->PushConstantRef<float>("Drag", mDrag);
	commandBuffer->PushConstantRef<float>("SpringK", mStiffness);
	commandBuffer->PushConstantRef<float>("SpringD", mDamping);
	commandBuffer->PushConstantRef<float>("DeltaTime", Scene()->FixedTimeStep());
	commandBuffer->PushConstantRef<uint32_t>("SphereCount", (uint32_t)mSphereColliders.size());
	commandBuffer->PushConstantRef<float3>("Gravity", mGravity);
	commandBuffer->PushConstantRef<float3>("Move", mMove);
	commandBuffer->DispatchAligned(triCount);


	b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.buffer = *mVelocityBuffer;
	b.size = mVelocityBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);
	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);


	ComputePipeline* integrate = mPin ? shader->GetCompute("Integrate", { "PIN" }) : shader->GetCompute("Integrate", {});
	ds = commandBuffer->GetDescriptorSet("Integrate0", integrate->mDescriptorSetLayouts[0]);
	if (mPin) ds->CreateStorageBufferDescriptor(m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, integrate->GetDescriptorLocation("SourceVertices"));
	ds->CreateUniformBufferDescriptor(objBuffer, integrate->GetDescriptorLocation("ObjectBuffer"));
	ds->CreateStorageBufferDescriptor(mVertexBuffer, integrate->GetDescriptorLocation("Vertices"));
	ds->CreateStorageBufferDescriptor(mVelocityBuffer, integrate->GetDescriptorLocation("Velocities"));
	ds->CreateStorageBufferDescriptor(mForceBuffer, integrate->GetDescriptorLocation("Forces"));
	ds->CreateStorageBufferDescriptor(sphereBuffer, integrate->GetDescriptorLocation("Spheres"));
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(integrate);
	commandBuffer->DispatchAligned(m->VertexCount());


	b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);


	ComputePipeline* normals = m->IndexType() == VK_INDEX_TYPE_UINT16 ? shader->GetCompute("ComputeNormals0", {}) : shader->GetCompute("ComputeNormals0", { "INDEX_UINT32" });
	ds = commandBuffer->GetDescriptorSet("Normals0", normals->mDescriptorSetLayouts[0]);
	ds->CreateStorageBufferDescriptor(mVertexBuffer, normals->GetDescriptorLocation("Verticesu"));
	ds->CreateStorageBufferDescriptor(m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, normals->GetDescriptorLocation("SourceVertices"));
	ds->CreateStorageBufferDescriptor(m->IndexBuffer().get(), baseIndex, m->IndexBuffer()->Size() - baseIndex, normals->GetDescriptorLocation("Triangles"));
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(normals);
	commandBuffer->DispatchAligned(triCount);


	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);


	ComputePipeline* normals2 = shader->GetCompute("ComputeNormals1", {});
	ds = commandBuffer->GetDescriptorSet("Normals1", normals2->mDescriptorSetLayouts[0]);
	ds->CreateStorageBufferDescriptor(mVertexBuffer, normals2->GetDescriptorLocation("Vertices"));
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(normals2);
	commandBuffer->DispatchAligned(m->VertexCount());


	b.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, b);
}

void ClothRenderer::OnLateUpdate(CommandBuffer* commandBuffer) {
	if (!mVertexBuffer) return;
	VkBufferMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	barrier.buffer = *mVertexBuffer;
	barrier.size = mVertexBuffer->Size();
	commandBuffer->Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, barrier);
}
void ClothRenderer::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	::Mesh* mesh = this->Mesh();

	commandBuffer->BindMaterial(mMaterial.get(), mesh->VertexInput(), mesh->Topology());
	GraphicsPipeline* pipeline = mMaterial->GetPassPipeline(commandBuffer->CurrentShaderPass());

	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT] != VK_NULL_HANDLE) {
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
	vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), 0, 0);
	commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	if (camera->StereoMode() != STEREO_NONE) {
		camera->SetViewportScissor(commandBuffer, EYE_RIGHT);
		vkCmdDrawIndexed(*commandBuffer, mesh->IndexCount(), 1, mesh->BaseIndex(), 0, 0);
		commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	}
}