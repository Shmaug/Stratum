#include <Scene/Renderers/ClothRenderer.hpp>

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

		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mVelocityBuffer = new Buffer(mName + "Velocities", m->VertexBuffer()->Device(), m->VertexCount() * sizeof(float4), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mEdgeBuffer = new Buffer(mName + "Edges", m->VertexBuffer()->Device(), (((x+x)*(x+x+1)/2+x)+31)/32, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
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

		mVertexBuffer = new Buffer(mName + "Vertices", m->VertexBuffer()->Device(), m->VertexBuffer()->Size(), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mVelocityBuffer = new Buffer(mName + "Velocities", m->VertexBuffer()->Device(), m->VertexCount() * sizeof(float4), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mForceBuffer = new Buffer(mName + "Forces", m->VertexBuffer()->Device(), sizeof(float4) * m->VertexCount(), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
		mEdgeBuffer = new Buffer(mName + "Edges", m->VertexBuffer()->Device(), (((x+x)*(x+x+1)/2+x)+31)/32, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
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

	vk::BufferMemoryBarrier b = {};
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	b.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

	if (mCopyVertices) {
		vk::BufferCopy cpy = {};
		cpy.srcOffset = m->VertexSize() * m->BaseVertex();
		cpy.size = mVertexBuffer->Size();
		((vk::CommandBuffer)*commandBuffer).copyBuffer(*m->VertexBuffer(), *mVertexBuffer, 1, &cpy);
		((vk::CommandBuffer)*commandBuffer).fillBuffer(*mVelocityBuffer, 0, mVelocityBuffer->Size(), 0);

		b.srcAccessMask = vk::AccessFlagBits::eTransferWrite;

		b.buffer = *mVertexBuffer;
		b.size = mVertexBuffer->Size();
		commandBuffer->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, b);

		b.buffer = *mVelocityBuffer;
		b.size = mVelocityBuffer->Size();
		commandBuffer->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, b);

		mCopyVertices = false;
	}

	((vk::CommandBuffer)*commandBuffer).fillBuffer(*mForceBuffer, 0, mForceBuffer->Size(), 0);
	((vk::CommandBuffer)*commandBuffer).fillBuffer(*mEdgeBuffer, 0, mEdgeBuffer->Size(), 0);
	b.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	b.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, b);
	b.buffer = *mEdgeBuffer;
	b.size = mEdgeBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, b);

	Pipeline* shader = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/cloth.stmb");

	Buffer* sphereBuffer = commandBuffer->GetBuffer("Cloth Spheres", sizeof(float4) * max(1u, (uint32_t)mSphereColliders.size()), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
	for (uint32_t i = 0; i < mSphereColliders.size(); i++)
		((float4*)sphereBuffer->MappedData())[i] = float4(mSphereColliders[i].first->WorldPosition(), mSphereColliders[i].second);
	
	Buffer* objBuffer = commandBuffer->GetBuffer("Cloth Obj", sizeof(float4x4) * 2, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
	((float4x4*)objBuffer->MappedData())[0] = ObjectToWorld();
	((float4x4*)objBuffer->MappedData())[1] = WorldToObject();

	uint32_t triCount = m->IndexCount()/3;
	uint32_t vs = sizeof(StdVertex);

	vk::DeviceSize baseVertex = m->BaseVertex() * m->VertexSize();
	vk::DeviceSize baseIndex = m->BaseIndex() * (m->IndexType() == vk::IndexType::eUint16 ? sizeof(uint16_t) : sizeof(uint32_t));

	ComputePipeline* add = m->IndexType() == vk::IndexType::eUint16 ? shader->GetCompute("AddForces", {}) : shader->GetCompute("AddForces", {"INDEXUint32"});
	DescriptorSet* ds = commandBuffer->GetDescriptorSet("AddForces", add->mDescriptorSetLayouts[0]);
	ds->CreateBufferDescriptor("ObjectBuffer", objBuffer, add);
	ds->CreateBufferDescriptor("SourceVertices", m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, add);
	ds->CreateBufferDescriptor("Triangles", m->IndexBuffer().get(), baseIndex, m->IndexBuffer()->Size() - baseIndex, add);
	ds->CreateBufferDescriptor("Vertices", mVertexBuffer, add);
	ds->CreateBufferDescriptor("Velocities", mVelocityBuffer, add);
	ds->CreateBufferDescriptor("Forces", mForceBuffer, add);
	ds->CreateBufferDescriptor("Edges", mEdgeBuffer, add);
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


	b.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
	b.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	b.buffer = *mVelocityBuffer;
	b.size = mVelocityBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, b);
	b.buffer = *mForceBuffer;
	b.size = mForceBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, b);


	ComputePipeline* integrate = mPin ? shader->GetCompute("Integrate", { "PIN" }) : shader->GetCompute("Integrate", {});
	ds = commandBuffer->GetDescriptorSet("Integrate0", integrate->mDescriptorSetLayouts[0]);
	if (mPin) ds->CreateBufferDescriptor("SourceVertices", m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, integrate);
	ds->CreateBufferDescriptor("ObjectBuffer", objBuffer, integrate);
	ds->CreateBufferDescriptor("Vertices", mVertexBuffer, integrate);
	ds->CreateBufferDescriptor("Velocities", mVelocityBuffer, integrate);
	ds->CreateBufferDescriptor("Forces", mForceBuffer, integrate);
	ds->CreateBufferDescriptor("Spheres", sphereBuffer, integrate);
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(integrate);
	commandBuffer->DispatchAligned(m->VertexCount());


	b.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	b.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, b);


	ComputePipeline* normals = m->IndexType() == vk::IndexType::eUint16 ? shader->GetCompute("ComputeNormals0", {}) : shader->GetCompute("ComputeNormals0", { "INDEXUint32" });
	ds = commandBuffer->GetDescriptorSet("Normals0", normals->mDescriptorSetLayouts[0]);
	ds->CreateBufferDescriptor("Verticesu", mVertexBuffer, normals);
	ds->CreateBufferDescriptor("SourceVertices", m->VertexBuffer().get(), baseVertex, m->VertexBuffer()->Size() - baseVertex, normals);
	ds->CreateBufferDescriptor("Triangles", m->IndexBuffer().get(), baseIndex, m->IndexBuffer()->Size() - baseIndex, normals);
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(normals);
	commandBuffer->DispatchAligned(triCount);


	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, b);


	ComputePipeline* normals2 = shader->GetCompute("ComputeNormals1", {});
	ds = commandBuffer->GetDescriptorSet("Normals1", normals2->mDescriptorSetLayouts[0]);
	ds->CreateBufferDescriptor("Vertices", mVertexBuffer, normals2);
	commandBuffer->BindDescriptorSet(ds, 0);
	commandBuffer->BindPipeline(normals2);
	commandBuffer->DispatchAligned(m->VertexCount());


	b.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
	b.buffer = *mVertexBuffer;
	b.size = mVertexBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, b);
}

void ClothRenderer::OnLateUpdate(CommandBuffer* commandBuffer) {
	if (!mVertexBuffer) return;
	vk::BufferMemoryBarrier barrier = {};
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
	barrier.buffer = *mVertexBuffer;
	barrier.size = mVertexBuffer->Size();
	commandBuffer->Barrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexInput, barrier);
}
void ClothRenderer::OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) {
	::Mesh* mesh = this->Mesh();

	commandBuffer->BindMaterial(mMaterial.get(), mesh->VertexInput(), mesh->Topology());
	GraphicsPipeline* pipeline = mMaterial->GetPassPipeline(commandBuffer->CurrentShaderPass());

	if (pipeline->mDescriptorSetLayouts.size() > PER_OBJECT && pipeline->mDescriptorSetLayouts[PER_OBJECT]) {
		Buffer* instanceBuffer = commandBuffer->GetBuffer(mName, sizeof(InstanceBuffer), vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
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
	
	camera->SetViewportScissor(commandBuffer, StereoEye::eLeft);
	((vk::CommandBuffer)*commandBuffer).drawIndexed(mesh->IndexCount(), 1, mesh->BaseIndex(), 0, 0);
	commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	if (camera->StereoMode() != StereoMode::eNone) {
		camera->SetViewportScissor(commandBuffer, StereoEye::eRight);
		((vk::CommandBuffer)*commandBuffer).drawIndexed(mesh->IndexCount(), 1, mesh->BaseIndex(), 0, 0);
		commandBuffer->mTriangleCount += mesh->IndexCount() / 3;
	}
}