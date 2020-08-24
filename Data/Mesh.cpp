#include <Data/Mesh.hpp>
#include <Scene/Bone.hpp>
#include <Scene/TriangleBvh2.hpp>

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#pragma warning(disable:26451)

using namespace std;

const ::VertexInput StdVertex::VertexInput {
	{
		{
			0, // binding
			sizeof(StdVertex), // stride
			VK_VERTEX_INPUT_RATE_VERTEX // inputRate
		}
	},
	{
		{
			0, // location
			0, // binding
			vk::Format::eR32G32B32Sfloat, // format
			offsetof(StdVertex, position) // offset
		},
		{
			1, // location
			0, // binding
			vk::Format::eR32G32B32Sfloat, // format
			offsetof(StdVertex, normal) // offset
		},
		{
			2, // location
			0, // binding
			vk::Format::eR32G32B32A32Sfloat, // format
			offsetof(StdVertex, tangent) // offset
		},
		{
			3, // location
			0, // binding
			vk::Format::eR32G32Sfloat, // format
			offsetof(StdVertex, uv) // offset
		}
	}
};

const ::VertexInput Float3VertexInput{
	{
		{
			0, // binding
			sizeof(float3), // stride
			VK_VERTEX_INPUT_RATE_VERTEX // inputRate
		}
	},
	{
		{
			0, // location
			0, // binding
			vk::Format::eR32G32B32Sfloat, // format
			0 // offset
		}
	}
};

struct AIWeight {
	string bones[4];
	float4 weights;

	AIWeight() {
		bones[0] = bones[1] = bones[2] = bones[3] = "";
		weights[0] = weights[1] = weights[2] = weights[3] = 0.f;
	}

	inline void SetWeight(const std::string& cluster, float weight) {
		if (weight < .001f) return;
		uint32_t index = 0;
		float m = weights[0];
		for (uint32_t i = 0; i < 4; i++) {
			if (cluster == bones[i]) {
				index = i;
				break;
			} else if (weights[i] < m) {
				index = i;
				m = weights[i];
			}
		}

		bones[index] = cluster;
		weights[index] = weight;
	}

	inline void NormalizeWeights() {
		weights /= dot(float4(1), weights);
	}
};

inline uint32_t GetDepth(aiNode* node) {
	uint32_t d = 0;
	while (node->mParent) {
		node = node->mParent;
		d++;
	}
	return d;
}
inline float4x4 ConvertMatrix(const aiMatrix4x4& m) {
	return float4x4(
		m.a1, m.b1, m.c1, m.d1,
		m.a2, m.b2, m.c2, m.d2,
		m.a3, m.b3, m.c3, m.d3,
		m.a4, m.b4, m.c4, m.d4
	);
}
inline Bone* AddBone(AnimationRig& rig, aiNode* node, const aiScene* scene, aiNode* root, unordered_map<aiNode*, Bone*>& boneMap, float scale) {
	if (node == root) return nullptr;
	if (boneMap.count(node))
		return boneMap.at(node);
	
	float4x4 mat = ConvertMatrix(node->mTransformation);
	Bone* parent = nullptr;

	if (node->mParent) {
		// merge empty bones
		aiNode* p = node->mParent;
		while (p && p->mName == aiString("")) {
			mat = mat * ConvertMatrix(p->mTransformation);
			p = p->mParent;
		}
		// parent transform is the first non-empty parent bone
		if (p) parent = AddBone(rig, p, scene, root, boneMap, scale);
	}

	quaternion q;
	q.x = mat[2].y - mat[1].z;
	q.y = mat[0].z - mat[2].x;
	q.z = mat[1].x - mat[0].y;
	q.w = sqrtf(1.f + mat[0].x + mat[1].y + mat[2].z) * .5f;
	q.xyz /= 4.f * q.w;

	Bone* bone = new Bone(node->mName.C_Str(), (uint32_t)rig.size());
	boneMap.emplace(node, bone);
	rig.push_back(bone);
	bone->LocalPosition(mat[3].xyz * scale);
	bone->LocalRotation(q);
	bone->LocalScale(length(mat[0].xyz), length(mat[1].xyz), length(mat[2].xyz));

	if (parent) parent->AddChild(bone);
	return bone;
}

Mesh::Mesh(const string& name) : mName(name) {}
Mesh::Mesh(const string& name, ::Device* device, const string& filename, float scale) : mName(name) {
	const aiScene* scene = aiImportFile(filename.c_str(), aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_FlipUVs | aiProcess_MakeLeftHanded);
	if (!scene) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to open %s: %s\n", filename.c_str(), aiGetErrorString());
		throw;
	}
	vector<StdVertex> vertices;
	vector<uint16_t> indices16;
	vector<uint32_t> indices32;
	float3 mn, mx;

	vector<AIWeight> weights;
	unordered_map<string, aiBone*> uniqueBones;

	uint32_t vertexCount = 0;
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
		vertexCount += scene->mMeshes[m]->mNumVertices;
	bool use32bit = true;// vertexCount > 0xFFFF;

	// append vertices, keep track of bounding box
	for (uint32_t i = 0; i < scene->mNumMeshes; i++) {
		const aiMesh* mesh = scene->mMeshes[i];
		uint32_t baseIndex = (uint32_t)vertices.size();
		
		if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) continue;

		for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
			StdVertex vertex = {};
			memset(&vertex, 0, sizeof(StdVertex));

			vertex.position = { (float)mesh->mVertices[i].x, (float)mesh->mVertices[i].y, (float)mesh->mVertices[i].z };
			if (mesh->HasNormals()) vertex.normal = { (float)mesh->mNormals[i].x, (float)mesh->mNormals[i].y, (float)mesh->mNormals[i].z };
			if (mesh->HasTangentsAndBitangents()) {
				vertex.tangent = { (float)mesh->mTangents[i].x, (float)mesh->mTangents[i].y, (float)mesh->mTangents[i].z, 1.f };
				float3 bt = float3((float)mesh->mBitangents[i].x, (float)mesh->mBitangents[i].y, (float)mesh->mBitangents[i].z);
				vertex.tangent.w = dot(cross(vertex.tangent.xyz, vertex.normal), bt) > 0.f ? 1.f : -1.f;
			}
			if (mesh->HasTextureCoords(0)) vertex.uv = { (float)mesh->mTextureCoords[0][i].x, (float)mesh->mTextureCoords[0][i].y };
			vertex.position *= scale;

			if (i == 0) {
				mn = vertex.position;
				mx = vertex.position;
			} else {
				mn = min(vertex.position, mn);
				mx = max(vertex.position, mx);
			}

			vertices.push_back(vertex);
			weights.push_back(AIWeight());
		}

		if (use32bit)
			for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
				const aiFace& f = mesh->mFaces[i];
				if (f.mNumIndices == 0) continue;
				indices32.push_back(f.mIndices[0]);
				if (f.mNumIndices == 2) indices32.push_back(f.mIndices[1]);
				for (uint32_t j = 2; j < f.mNumIndices; j++) {
					indices32.push_back(f.mIndices[j - 1]);
					indices32.push_back(f.mIndices[j]);
				}
			} else {
				for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
					const aiFace& f = mesh->mFaces[i];
					if (f.mNumIndices == 0) continue;
					indices16.push_back(f.mIndices[0]);
					if (f.mNumIndices == 2) indices16.push_back(f.mIndices[1]);
					for (uint32_t j = 2; j < f.mNumIndices; j++) {
						indices16.push_back(f.mIndices[j - 1]);
						indices16.push_back(f.mIndices[j]);
					}
				}
			}

		if (mesh->HasBones())
			for (uint16_t c = 0; c < mesh->mNumBones; c++) {
				aiBone* bone = mesh->mBones[c];
				for (uint32_t i = 0; i < bone->mNumWeights; i++) {
					uint32_t index = baseIndex + bone->mWeights[i].mVertexId;
					weights[index].SetWeight(bone->mName.C_Str(), (float)bone->mWeights[i].mWeight);
				}

				if (uniqueBones.count(bone->mName.C_Str()) == 0)
					uniqueBones.emplace(bone->mName.C_Str(), bone);
			}
	}

	if (uniqueBones.size()) {
		unordered_map<aiNode*, Bone*> boneMap;

		// find animation root
		aiNode* root = scene->mRootNode;
		uint32_t rootDepth = 0xFFFF;
		for (auto& b : uniqueBones) {
			aiNode* node = scene->mRootNode->FindNode(b.second->mName);
			while (node&& node->mName == aiString(""))
				node = node->mParent;
			uint32_t d = GetDepth(node);
			if (d < rootDepth) {
				rootDepth = d;

				while (node->mParent&& node->mParent->mName == aiString(""))
					node = node->mParent;
				root = node->mParent;
			}
		}

		AnimationRig rig;

		// compute bone matrices and bonesByName
		unordered_map<string, uint32_t> bonesByName;
		for (auto& b : uniqueBones) {
			aiNode* node = scene->mRootNode->FindNode(b.second->mName);
			Bone* bone = AddBone(rig, node, scene, root, boneMap, scale);
			if (!bone) continue;
			BoneTransform bt;
			ConvertMatrix(b.second->mOffsetMatrix).Decompose(&bt.mPosition, &bt.mRotation, &bt.mScale);
			bt.mPosition *= scale;
			bone->mInverseBind = inverse(float4x4::TRS(bt.mPosition, bt.mRotation, bt.mScale));
			bonesByName.emplace(b.second->mName.C_Str(), bone->mBoneIndex);
		}

		float4x4 rootTransform(1.f);
		while (root) {
			rootTransform = rootTransform * ConvertMatrix(root->mTransformation);
			root = root->mParent;
		}
		BoneTransform roott;
		rootTransform.Decompose(&roott.mPosition, &roott.mRotation, &roott.mScale);
		roott.mPosition *= scale;

		for (auto& b : rig) {
			if (!b->Parent()) {
				BoneTransform bt {
					b->LocalPosition(),
					b->LocalRotation(),
					b->LocalScale()
				};
				bt = roott * bt;
				b->LocalPosition(bt.mPosition);
				b->LocalRotation(bt.mRotation);
				b->LocalScale(bt.mScale);
			}
		}

		for (uint32_t i = 0; i < scene->mNumAnimations; i++) {
			const aiAnimation* anim = scene->mAnimations[i];
			throw;
			//mAnimations.emplace(anim->mName.C_Str(), new Animation(anim, bonesByName, scale));
		}

		vector<VertexWeight> vertexWeights(vertices.size());
		for (uint32_t i = 0; i < vertices.size(); i++) {
			weights[i].NormalizeWeights();
			for (unsigned int j = 0; j < 4; j++) {
				if (bonesByName.count(weights[i].bones[j])) {
					vertexWeights[i].Indices[j] = bonesByName.at(weights[i].bones[j]);
					vertexWeights[i].Weights[j] = weights[i].weights[j];
				}
			}
		}

		mWeightBuffer = make_shared<Buffer>(mName + " Weights", device, vertexWeights.size() * sizeof(VertexWeight), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
	}
	
	if (use32bit) {
		mIndexCount = (uint32_t)indices32.size();
		mIndexType = vk::IndexType::eUint32;
	} else {
		mIndexCount = (uint32_t)indices16.size();
		mIndexType = vk::IndexType::eUint16;
	}

	aiReleaseImport(scene);

	mVertexCount = (uint32_t)vertices.size();
	mBounds = AABB(mn, mx);
	mVertexSize = sizeof(StdVertex);
	mVertexInput = &StdVertex::VertexInput;

	mBvh = new TriangleBvh2();
	if (use32bit)
		mBvh->Build(vertices.data(), 0, vertexCount, sizeof(StdVertex), indices32.data(), (uint32_t)indices32.size(), vk::IndexType::eUint32);
	else
		mBvh->Build(vertices.data(), 0, vertexCount, sizeof(StdVertex), indices16.data(), (uint32_t)indices16.size(), vk::IndexType::eUint16);

	if (!uniqueBones.size())
		mWeightBuffer = nullptr;
	mVertexBuffer = make_shared<Buffer>(name + " Vertex Buffer", device, vertices.data(), sizeof(StdVertex) * vertices.size(), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer);
	if (use32bit)
		mIndexBuffer = make_shared<Buffer>(name + " Index Buffer", device, indices32.data(), sizeof(uint32_t) * indices32.size(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);
	else
		mIndexBuffer = make_shared<Buffer>(name + " Index Buffer", device, indices16.data(), sizeof(uint16_t) * indices16.size(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);

	printf("Loaded %s / %d verts %d tris / %.2fx%.2fx%.2f\n", filename.c_str(), (int)vertices.size(), (int)(use32bit ? indices32.size() : indices16.size()) / 3, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
}
Mesh::Mesh(const string& name, ::Device* device, const AABB& bounds, TriangleBvh2* bvh, shared_ptr<Buffer> vertexBuffer, shared_ptr<Buffer> indexBuffer,
	uint32_t baseVertex, uint32_t vertexCount, uint32_t baseIndex, uint32_t indexCount, const ::VertexInput* vertexInput, vk::IndexType indexType, vk::PrimitiveTopology topology)
	: mName(name), mVertexInput(vertexInput), mBvh(bvh), mBaseIndex(baseIndex), mIndexCount(indexCount), mIndexType(indexType), mBaseVertex(baseVertex), mVertexCount(vertexCount), mBounds(bounds), mTopology(topology) {
	
	mVertexBuffer = vertexBuffer;
	mIndexBuffer = indexBuffer;
	mWeightBuffer = nullptr;
	mVertexSize = 0;
	for (const auto& a : vertexInput->mAttributes)
		mVertexSize = max(mVertexSize, a.offset + ElementSize(a.format));
}
Mesh::Mesh(const string& name, ::Device* device, const AABB& bounds, TriangleBvh2* bvh, shared_ptr<Buffer> vertexBuffer, shared_ptr<Buffer> indexBuffer, shared_ptr<Buffer> weightBuffer,
	uint32_t baseVertex, uint32_t vertexCount, uint32_t baseIndex, uint32_t indexCount, const ::VertexInput* vertexInput, vk::IndexType indexType, vk::PrimitiveTopology topology)
	: mName(name), mVertexInput(vertexInput), mBvh(bvh), mBaseIndex(baseIndex), mIndexCount(indexCount), mIndexType(indexType), mBaseVertex(baseVertex), mVertexCount(vertexCount), mBounds(bounds), mTopology(topology) {

	mVertexBuffer = vertexBuffer;
	mIndexBuffer = indexBuffer;
	mWeightBuffer = weightBuffer;
	mVertexSize = 0;
	for (const auto& a : vertexInput->mAttributes)
		mVertexSize = max(mVertexSize, a.offset + ElementSize(a.format));
}
Mesh::Mesh(const string& name, ::Device* device, const void* vertices, const void* indices, uint32_t vertexCount, uint32_t vertexSize, uint32_t indexCount, const ::VertexInput* vertexInput, vk::IndexType indexType, vk::PrimitiveTopology topology)
	: mName(name), mVertexInput(vertexInput), mBvh(nullptr), mIndexCount(indexCount), mIndexType(indexType), mVertexCount(vertexCount), mVertexSize(vertexSize), mBaseVertex(0), mBaseIndex(0), mTopology(topology) {
	
	float3 mn, mx;
	for (uint32_t i = 0; i < indexCount; i++) {
		uint32_t index;
		if (mIndexType == vk::IndexType::eUint32)
			index = ((uint32_t*)indices)[i];
		else
			index = ((uint16_t*)indices)[i];

		const float3& pos = *(float3*)((uint8_t*)vertices + vertexSize * index);
		if (i == 0)
			mn = mx = pos;
		else {
			mn = min(pos, mn);
			mx = max(pos, mx);
		}
	}

	uint32_t indexSize = mIndexType == vk::IndexType::eUint32 ? sizeof(uint32_t) : sizeof(uint16_t);

	if (mTopology == vk::PrimitiveTopology::eTriangleList) {
		mBvh = new TriangleBvh2();
		mBvh->Build(vertices, 0, vertexCount, vertexSize, indices, indexCount, mIndexType);
	}

	mBounds = AABB(mn, mx);
	mVertexBuffer = make_shared<Buffer>(name + " Vertex Buffer", device, vertices, vertexSize * vertexCount, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer);
	mIndexBuffer  = make_shared<Buffer>(name + " Index Buffer", device, indices, indexSize * indexCount, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer);
}
Mesh::Mesh(const string& name, ::Device* device, const void* vertices, const VertexWeight* weights, const vector<pair<string, const void*>>&  shapeKeys, const void* indices, uint32_t vertexCount, uint32_t vertexSize, uint32_t indexCount, const ::VertexInput* vertexInput, vk::IndexType indexType, vk::PrimitiveTopology topology)
	: mName(name), mVertexInput(vertexInput),  mIndexCount(indexCount), mIndexType(indexType), mVertexCount(vertexCount), mVertexSize(vertexSize), mTopology(topology) {

	float3 mn, mx;
	for (uint32_t i = 0; i < indexCount; i++) {
		uint32_t index;
		if (mIndexType == vk::IndexType::eUint32)
			index = ((uint32_t*)indices)[i];
		else
			index = ((uint16_t*)indices)[i];

		const float3& pos = *(float3*)((uint8_t*)vertices + vertexSize * index);
		if (i == 0)
			mn = mx = pos;
		else {
			mn = min(pos, mn);
			mx = max(pos, mx);
		}
	}

	uint32_t indexSize = mIndexType == vk::IndexType::eUint32 ? sizeof(uint32_t) : sizeof(uint16_t);

	if (mTopology == vk::PrimitiveTopology::eTriangleList) {
		mBvh = new TriangleBvh2();
		mBvh->Build(vertices, 0, vertexCount, vertexSize, indices, indexCount, mIndexType);
	}

	mBounds = AABB(mn, mx);
	mVertexBuffer = make_shared<Buffer>(name + " Vertex Buffer", device, vertices, vertexSize * vertexCount, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);
	mWeightBuffer = make_shared<Buffer>(name + " Weight Buffer", device, weights, sizeof(VertexWeight) * vertexCount, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);
	mIndexBuffer = make_shared<Buffer>(name + " Index Buffer", device, indices, indexSize * indexCount, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer);

	for (auto i : shapeKeys)
		mShapeKeys.emplace(i.first, make_shared<Buffer>(name + i.first, device, i.second, vertexSize * vertexCount, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc));
}

Mesh* Mesh::CreatePlaneX(const string& name, Device* device, float s, float v) {
	float3 dpdu(0, 0, 2*s / v);
	const StdVertex verts[4] {
		{ float3(0, -s, -s), float3(1,0,0), float4(0,0,v,1), float2(0,0) },
		{ float3(0,  s, -s), float3(1,0,0), float4(0,0,v,1), float2(v,0) },
		{ float3(0, -s,  s), float3(1,0,0), float4(0,0,v,1), float2(0,v) },
		{ float3(0,  s,  s), float3(1,0,0), float4(0,0,v,1), float2(v,v) }
	};
	const uint32_t indices[6]{
		0,2,1,2,3,1
	};
	return new Mesh(name, device, verts, indices, 4, sizeof(StdVertex), 6, &StdVertex::VertexInput, vk::IndexType::eUint32);
}
Mesh* Mesh::CreatePlaneY(const string& name, Device* device, float s, float v) {
	float3 dpdu(2*s / v, 0, 0);
	const StdVertex verts[4] {
		{ float3(-s, 0, -s), float3(0,1,0), float4(dpdu,1), float2(0,0) },
		{ float3( s, 0, -s), float3(0,1,0), float4(dpdu,1), float2(v,0) },
		{ float3(-s, 0,  s), float3(0,1,0), float4(dpdu,1), float2(0,v) },
		{ float3( s, 0,  s), float3(0,1,0), float4(dpdu,1), float2(v,v) }
	};
	const uint32_t indices[6]{
		0,1,2,3,2,1
	};
	return new Mesh(name, device, verts, indices, 4, sizeof(StdVertex), 6, &StdVertex::VertexInput, vk::IndexType::eUint32);
}
Mesh* Mesh::CreatePlaneZ(const string& name, Device* device, float s, float v) {
	float3 dpdu(2*s / v, 0, 0);
	const StdVertex verts[4] {
		{ float3(-s, -s, 0), float3(0,0,1), float4(dpdu,1), float2(0,0) },
		{ float3( s, -s, 0), float3(0,0,1), float4(dpdu,1), float2(v,0) },
		{ float3(-s,  s, 0), float3(0,0,1), float4(dpdu,1), float2(0,v) },
		{ float3( s,  s, 0), float3(0,0,1), float4(dpdu,1), float2(v,v) }
	};
	const uint32_t indices[6]{
		0,2,1,2,3,1
	};
	return new Mesh(name, device, verts, indices, 4, sizeof(StdVertex), 6, &StdVertex::VertexInput, vk::IndexType::eUint32);
}
Mesh* Mesh::CreateCube(const string& name, Device* device, float r, float v) {
	float dpdu = 2*r / v;
	const StdVertex verts[24]{
		// front
		{ float3(-r, -r,  r), float3(0,0,1), float4(dpdu,0,0,1), float2(0,0) },
		{ float3( r, -r,  r), float3(0,0,1), float4(dpdu,0,0,1), float2(v,0) },
		{ float3(-r,  r,  r), float3(0,0,1), float4(dpdu,0,0,1), float2(0,v) },
		{ float3( r,  r,  r), float3(0,0,1), float4(dpdu,0,0,1), float2(v,v) },
		
		// back
		{ float3(-r, -r, -r), float3(0,0,-1), float4(dpdu,0,0,1), float2(0,0) },
		{ float3(-r,  r, -r), float3(0,0,-1), float4(dpdu,0,0,1), float2(0,v) },
		{ float3( r, -r, -r), float3(0,0,-1), float4(dpdu,0,0,1), float2(v,0) },
		{ float3( r,  r, -r), float3(0,0,-1), float4(dpdu,0,0,1), float2(v,v) },

		// right
		{ float3(r, -r, -r), float3(1,0,0), float4(0,0,dpdu,1), float2(0,0) },
		{ float3(r,  r, -r), float3(1,0,0), float4(0,0,dpdu,1), float2(v,0) },
		{ float3(r, -r,  r), float3(1,0,0), float4(0,0,dpdu,1), float2(0,v) },
		{ float3(r,  r,  r), float3(1,0,0), float4(0,0,dpdu,1), float2(v,v) },

		// left
		{ float3(-r, -r, -r), float3(-1,0,0), float4(0,0,-dpdu,1), float2(0,0) },
		{ float3(-r, -r,  r), float3(-1,0,0), float4(0,0,-dpdu,1), float2(0,v) },
		{ float3(-r,  r, -r), float3(-1,0,0), float4(0,0,-dpdu,1), float2(v,0) },
		{ float3(-r,  r,  r), float3(-1,0,0), float4(0,0,-dpdu,1), float2(v,v) },

		// top
		{ float3(-r, r, -r), float3(0,1,0), float4(dpdu,0,0,1), float2(0,0) },
		{ float3(-r, r,  r), float3(0,1,0), float4(dpdu,0,0,1), float2(0,v) },
		{ float3( r, r, -r), float3(0,1,0), float4(dpdu,0,0,1), float2(v,0) },
		{ float3( r, r,  r), float3(0,1,0), float4(dpdu,0,0,1), float2(v,v) },

		// bottom
		{ float3(-r, -r, -r), float3(0,-1,0), float4(dpdu,0,0,1), float2(0,0) },
		{ float3( r, -r, -r), float3(0,-1,0), float4(dpdu,0,0,1), float2(v,0) },
		{ float3(-r, -r,  r), float3(0,-1,0), float4(dpdu,0,0,1), float2(0,v) },
		{ float3( r, -r,  r), float3(0,-1,0), float4(dpdu,0,0,1), float2(v,v) }
	};
	const uint32_t indices[36]{
		0 , 2,  1,  2,  3,  1,
		4 , 6,  5,  6,  7,  5,
		8 , 10, 9,  10, 11, 9,
		12, 14, 13, 14, 15, 13,
		16, 18, 17, 18, 19, 17,
		20, 22, 21, 22, 23, 21
	};
	return new Mesh(name, device, verts, indices, 24, sizeof(StdVertex), 36, &StdVertex::VertexInput, vk::IndexType::eUint32);
}

bool Mesh::Intersect(const Ray& ray, float* t, bool any) {
	if (!mBvh) return false;
	return mBvh->Intersect(ray, t, any);
}

Mesh::~Mesh() {
	for (auto kp : mAnimations)
		safe_delete(kp.second);
	safe_delete(mBvh);
}