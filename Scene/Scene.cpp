#include <Scene/Scene.hpp>
#include <Scene/Renderer.hpp>
#include <Scene/MeshRenderer.hpp>
#include <Scene/SkinnedMeshRenderer.hpp>
#include <Scene/GUI.hpp>
#include <Core/Instance.hpp>
#include <Util/Profiler.hpp>

#include <assimp/scene.h>
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

using namespace std;

#define INSTANCE_BATCH_SIZE 1024
#define MAX_GPU_LIGHTS 64

#define SHADOW_ATLAS_RESOLUTION 8192
#define SHADOW_RESOLUTION 4096

const ::VertexInput Float3VertexInput {
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
			VK_FORMAT_R32G32B32_SFLOAT, // format
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
	inline void Set(const std::string& cluster, float weight) {
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
	inline void Normalize() {
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
			mat = ConvertMatrix(p->mTransformation) * mat;
			p = p->mParent;
		}
		// parent transform is the first non-empty parent bone
		if (p) parent = AddBone(rig, p, scene, root, boneMap, scale);
	}

	quaternion q;
	float3 p;
	float3 s;
	mat.Decompose(&p, &q, &s);

	Bone* bone = new Bone(node->mName.C_Str(), (uint32_t)rig.size());
	boneMap.emplace(node, bone);
	rig.push_back(bone);
	bone->LocalPosition(p * scale);
	bone->LocalRotation(q);
	bone->LocalScale(s);

	if (parent) parent->AddChild(bone);
	return bone;
}

bool RendererCompare(Renderer* a, Renderer* b) {
	uint32_t qa = a->Visible() ? a->RenderQueue() : 0xFFFFFFFF;
	uint32_t qb = b->Visible() ? b->RenderQueue() : 0xFFFFFFFF;
	if (qa == qb && qa != 0xFFFFFFFF) {
		MeshRenderer* ma = dynamic_cast<MeshRenderer*>(a);
		MeshRenderer* mb = dynamic_cast<MeshRenderer*>(b);
		if (ma && mb)
			if (ma->Material() == mb->Material())
				return ma->Mesh() < mb->Mesh();
			else
				return ma->Material() < mb->Material();
	}
	return qa < qb;
};

Scene::Scene(::Instance* instance, ::InputManager* inputManager, ::PluginManager* pluginManager)
	: mInstance(instance), mInputManager(inputManager), mPluginManager(pluginManager), mLastBvhBuild(0), mBvhDirty(true),
	mFixedTimeStep(.0025f), mPhysicsTimeLimitPerFrame(.2f) , mFixedAccumulator(0), mDeltaTime(0), mTotalTime(0), mFps(0), mFrameTimeAccum(0), mFpsAccum(0) {

	mSkyboxMaterial = new Material("Skybox", mInstance->Device()->AssetManager()->LoadShader("Shaders/skybox.stm"));
	mBvh = new ObjectBvh2();

	mShadowAtlas = new Framebuffer("ShadowAtlas", mInstance->Device(),
		{ SHADOW_ATLAS_RESOLUTION, SHADOW_ATLAS_RESOLUTION }, {},
		VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_ATTACHMENT_LOAD_OP_LOAD);
	
	float r = .5f;
	float3 verts[8]{
		float3(-r, -r, -r),
		float3(r, -r, -r),
		float3(-r, -r,  r),
		float3(r, -r,  r),
		float3(-r,  r, -r),
		float3(r,  r, -r),
		float3(-r,  r,  r),
		float3(r,  r,  r),
	};
	uint16_t indices[36]{
		2,7,6,2,3,7,
		0,1,2,2,1,3,
		1,5,7,7,3,1,
		4,5,1,4,1,0,
		6,4,2,4,0,2,
		4,7,5,4,6,7
	};
	mSkyboxCube = new Mesh("SkyCube", mInstance->Device(), verts, indices, 8, sizeof(float3), 36, &Float3VertexInput, VK_INDEX_TYPE_UINT16);

	mEnvironmentTexture = mInstance->Device()->AssetManager()->WhiteTexture();

	mStartTime = mClock.now();
	mLastFrame = mClock.now();
}
Scene::~Scene(){
	safe_delete(mSkyboxCube);
	safe_delete(mBvh);


	while (mObjects.size())
		RemoveObject(mObjects[0].get());

	safe_delete(mSkyboxMaterial);

	for (Camera* c : mShadowCameras) safe_delete(c);

	safe_delete(mShadowAtlas);

	mCameras.clear();
	mRenderers.clear();
	mLights.clear();
	mObjects.clear();
}

Object* Scene::LoadModelScene(const string& filename,
	function<shared_ptr<Material>(Scene*, aiMaterial*)> materialSetupFunc,
	function<void(Scene*, Object*, aiMaterial*)> objectSetupFunc,
	float scale, float directionalLightIntensity, float spotLightIntensity, float pointLightIntensity) {
	const aiScene* scene = aiImportFile(filename.c_str(), aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_FlipUVs | aiProcess_MakeLeftHanded | aiProcess_SortByPType);
	if (!scene) {
		fprintf_color(COLOR_RED, stderr, "Failed to open %s: %s\n", filename.c_str(), aiGetErrorString());
		throw;
	}

	Object* root = nullptr;

	vector<shared_ptr<Mesh>> meshes;
	vector<shared_ptr<Material>> materials;
	unordered_map<aiNode*, Object*> objectMap;

	vector<AIWeight> weights;
	unordered_map<string, aiBone*> uniqueBones;

	vector<StdVertex> vertices;
	vector<uint32_t> indices;

	uint32_t totalVertices = 0;
	uint32_t totalIndices = 0;

	bool hasBones = false;

	for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
		const aiMesh* mesh = scene->mMeshes[m];
		totalVertices += mesh->mNumVertices;
		for (uint32_t i = 0; i < mesh->mNumFaces; i++)
			totalIndices += min(mesh->mFaces[i].mNumIndices, 3u);

		if (mesh->HasBones()) hasBones = true;
	}

	shared_ptr<Buffer> vertexBuffer = make_shared<Buffer>(filename + " Vertices", mInstance->Device(), sizeof(StdVertex) * totalVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	shared_ptr<Buffer> indexBuffer  = make_shared<Buffer>(filename + " Indices" , mInstance->Device(), sizeof(uint32_t) * totalIndices  , VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	shared_ptr<Buffer> weightBuffer = nullptr;
	if (hasBones) weightBuffer = make_shared<Buffer>(filename + " Weights", mInstance->Device(), sizeof(VertexWeight) * totalVertices, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for (uint32_t m = 0; m < scene->mNumMaterials; m++)
		materials.push_back(materialSetupFunc(this, scene->mMaterials[m]));

	for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
		const aiMesh* mesh = scene->mMeshes[m];

		if (mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE) {
			meshes.push_back(nullptr);
			continue;
		}

		VkPrimitiveTopology topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		uint32_t baseVertex = (uint32_t)vertices.size();
		uint32_t baseIndex  = (uint32_t)indices.size();

		// vertex data
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

			vertices.push_back(vertex);
			weights.push_back(AIWeight());
		}

		// index data
		float3 mn = vertices[baseVertex].position, mx = vertices[baseVertex].position;
		for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
			const aiFace& f = mesh->mFaces[i];
			indices.push_back(f.mIndices[0]);
			mn = min(vertices[baseVertex + f.mIndices[0]].position, mn);
			mx = max(vertices[baseVertex + f.mIndices[0]].position, mx);
			if (f.mNumIndices > 1) {
				indices.push_back(f.mIndices[1]);
				mn = min(vertices[baseVertex + f.mIndices[1]].position, mn);
				mx = max(vertices[baseVertex + f.mIndices[1]].position, mx);
				if (f.mNumIndices > 2) {
					indices.push_back(f.mIndices[2]);
					mn = min(vertices[baseVertex + f.mIndices[2]].position, mn);
					mx = max(vertices[baseVertex + f.mIndices[2]].position, mx);
				} else
					topo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			} else
				topo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		}

		uint32_t vertexCount = (uint32_t)vertices.size() - baseVertex;
		uint32_t indexCount  = (uint32_t)indices.size() - baseIndex;

		TriangleBvh2* bvh = nullptr;
		if (topo == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
			bvh = new TriangleBvh2();
			bvh->Build(vertices.data() + baseVertex, 0, vertexCount, sizeof(StdVertex), indices.data() + baseIndex, indexCount, VK_INDEX_TYPE_UINT32);
		}

		if (mesh->HasBones()) {
			for (uint16_t c = 0; c < mesh->mNumBones; c++) {
				aiBone* bone = mesh->mBones[c];
				for (uint32_t i = 0; i < bone->mNumWeights; i++) {
					uint32_t index = baseVertex + bone->mWeights[i].mVertexId;
					weights[index].Set(bone->mName.C_Str(), (float)bone->mWeights[i].mWeight);
				}
				if (uniqueBones.count(bone->mName.C_Str()) == 0)
					uniqueBones.emplace(bone->mName.C_Str(), bone);
			}

			meshes.push_back(make_shared<Mesh>(mesh->mName.C_Str(), mInstance->Device(),
				AABB(mn, mx), bvh, vertexBuffer, indexBuffer, weightBuffer, baseVertex, vertexCount, baseIndex, indexCount,
				&StdVertex::VertexInput, VK_INDEX_TYPE_UINT32, topo));
		} else {
			meshes.push_back(make_shared<Mesh>(mesh->mName.C_Str(), mInstance->Device(),
				AABB(mn, mx), bvh, vertexBuffer, indexBuffer, baseVertex, vertexCount, baseIndex, indexCount,
				&StdVertex::VertexInput, VK_INDEX_TYPE_UINT32, topo));
		}
	}

	AnimationRig rig;

	if (uniqueBones.size()) {
		unordered_map<aiNode*, Bone*> boneMap;

		// find root node
		aiNode* root = scene->mRootNode;
		uint32_t rootDepth = 0xFFFFFFFF;
		for (auto& b : uniqueBones) {
			aiNode* node = scene->mRootNode->FindNode(b.second->mName);
			while (node && node->mName == aiString(""))
				node = node->mParent;
			uint32_t d = GetDepth(node);
			if (d < rootDepth) {
				rootDepth = d;

				while (node->mParent && node->mParent->mName == aiString(""))
					node = node->mParent;
				root = node->mParent;
			}
		}

		// compute bone matrices and bonesByName
		unordered_map<string, uint32_t> bonesByName;
		for (auto& b : uniqueBones) {
			aiNode* node = scene->mRootNode->FindNode(b.second->mName);
			Bone* bone = AddBone(rig, node, scene, root, boneMap, scale);
			if (!bone) continue;
			BoneTransform bt;
			ConvertMatrix(b.second->mOffsetMatrix).Decompose(&bt.mPosition, &bt.mRotation, &bt.mScale);
			bt.mPosition *= scale;
			bone->mInverseBind = float4x4::TRS(bt.mPosition, bt.mRotation, bt.mScale);
			bonesByName.emplace(b.second->mName.C_Str(), bone->mBoneIndex);
		}
		/*
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
				BoneTransform bt{
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
		*/
		vector<VertexWeight> vertexWeights(vertices.size());
		for (uint32_t i = 0; i < vertices.size(); i++) {
			weights[i].Normalize();
			for (uint32_t j = 0; j < 4; j++) {
				if (bonesByName.count(weights[i].bones[j])) {
					vertexWeights[i].Indices[j] = bonesByName.at(weights[i].bones[j]);
					vertexWeights[i].Weights[j] = weights[i].weights[j];
				}
			}
		}

		weightBuffer->Upload(vertexWeights.data(), vertexWeights.size() * sizeof(VertexWeight));
	}

	vertexBuffer->Upload(vertices.data(), vertices.size() * sizeof(StdVertex));
	indexBuffer->Upload(indices.data(), indices.size() * sizeof(uint32_t));

	queue<pair<Object*, aiNode*>> nodes;
	nodes.push(make_pair((Object*)nullptr, scene->mRootNode));
	while (nodes.size()) {
		auto np = nodes.front();
		nodes.pop();
		aiNode* n = np.second;

		aiVector3D position;
		aiVector3D nscale;
		aiQuaternion rotation;
		n->mTransformation.Decompose(nscale, rotation, position);

		shared_ptr<Object> obj = make_shared<Object>(n->mName.C_Str());
		AddObject(obj);
		obj->LocalPosition(position.x * scale, position.y * scale, position.z * scale);
		obj->LocalRotation(quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
		obj->LocalScale(nscale.x, nscale.y, nscale.z);

		if (np.first) np.first->AddChild(obj.get());
		else root = obj.get();

		objectSetupFunc(this, obj.get(), nullptr);

		objectMap.emplace(n, obj.get());

		for (uint32_t i = 0; i < n->mNumMeshes; i++) {
			shared_ptr<Mesh> mesh = meshes[n->mMeshes[i]];
			if (!mesh) continue;
			uint32_t mat = scene->mMeshes[n->mMeshes[i]]->mMaterialIndex;

			if (mesh->WeightBuffer()) {
				shared_ptr<SkinnedMeshRenderer> smr = make_shared<SkinnedMeshRenderer>(n->mName.C_Str() + mesh->mName);
				smr->Rig(rig);
				AddObject(smr);
				smr->Material(mat < materials.size() ? materials[mat] : nullptr);
				smr->Mesh(mesh);
				obj->AddChild(smr.get());
				objectSetupFunc(this, smr.get(), scene->mMaterials[mat]);
			} else {
				shared_ptr<MeshRenderer> mr = make_shared<MeshRenderer>(n->mName.C_Str() + mesh->mName);
				AddObject(mr);
				mr->Material(mat < materials.size() ? materials[mat] : nullptr);
				mr->Mesh(mesh);
				obj->AddChild(mr.get());
				objectSetupFunc(this, mr.get(), scene->mMaterials[mat]);
			}
		}

		for (uint32_t i = 0; i < n->mNumChildren; i++)
			nodes.push({ obj.get(), n->mChildren[i] });
	}

	const float minAttenuation = .001f; // min light attenuation for computing range from infinite attenuation

	for (uint32_t i = 0; i < scene->mNumLights; i++) {
		switch (scene->mLights[i]->mType) {
		case aiLightSource_DIRECTIONAL:
		{
			if (directionalLightIntensity <= 0) break;

			aiNode* lightNode = scene->mRootNode->FindNode(scene->mLights[i]->mName);
			aiVector3D position;
			aiVector3D nscale;
			aiQuaternion rotation;
			lightNode->mTransformation.Decompose(nscale, rotation, position);

			float3 col = float3(scene->mLights[i]->mColorDiffuse.r, scene->mLights[i]->mColorDiffuse.g, scene->mLights[i]->mColorDiffuse.b);
			float li = length(col);

			shared_ptr<Light> light = make_shared<Light>(scene->mLights[i]->mName.C_Str());
			light->LocalRotation(quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
			light->Type(LIGHT_TYPE_SUN);
			light->Intensity(li * directionalLightIntensity);
			light->Color(col / li);
			light->CastShadows(true);
			AddObject(light);

			objectMap.at(lightNode->mParent)->AddChild(light.get());
			break;
		}
		case aiLightSource_SPOT:
		{
			if (spotLightIntensity <= 0) break;

			aiNode* lightNode = scene->mRootNode->FindNode(scene->mLights[i]->mName);
			aiVector3D position;
			aiVector3D nscale;
			aiQuaternion rotation;
			lightNode->mTransformation.Decompose(nscale, rotation, position);

			position += lightNode->mTransformation * scene->mLights[i]->mPosition;

			float a = scene->mLights[i]->mAttenuationQuadratic;
			float b = scene->mLights[i]->mAttenuationLinear;
			float c = scene->mLights[i]->mAttenuationConstant - (1 / minAttenuation);
			float3 col = float3(scene->mLights[i]->mColorDiffuse.r, scene->mLights[i]->mColorDiffuse.g, scene->mLights[i]->mColorDiffuse.b);
			float li = length(col);

			shared_ptr<Light> light = make_shared<Light>(scene->mLights[i]->mName.C_Str());
			light->LocalPosition(position.x * scale, position.y * scale, position.z * scale);
			light->LocalRotation(quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
			light->Type(LIGHT_TYPE_SPOT);
			light->InnerSpotAngle(scene->mLights[i]->mAngleInnerCone);
			light->OuterSpotAngle(scene->mLights[i]->mAngleOuterCone);
			light->Intensity(li * spotLightIntensity);
			light->Range((-b + sqrtf(b * b - 4 * a * c)) / (2 * a));
			light->Color(col / li);
			AddObject(light);

			objectMap.at(lightNode->mParent)->AddChild(light.get());
			break;
		}
		case aiLightSource_POINT:
		{
			if (pointLightIntensity <= 0) break;

			aiNode* lightNode = scene->mRootNode->FindNode(scene->mLights[i]->mName);
			aiVector3D position;
			aiVector3D nscale;
			aiQuaternion rotation;
			lightNode->mTransformation.Decompose(nscale, rotation, position);

			position += lightNode->mTransformation * scene->mLights[i]->mPosition;

			float a = scene->mLights[i]->mAttenuationQuadratic;
			float b = scene->mLights[i]->mAttenuationLinear;
			float c = scene->mLights[i]->mAttenuationConstant - (1 / minAttenuation);
			float3 col = float3(scene->mLights[i]->mColorDiffuse.r, scene->mLights[i]->mColorDiffuse.g, scene->mLights[i]->mColorDiffuse.b);
			float li = length(col);

			shared_ptr<Light> light = make_shared<Light>(scene->mLights[i]->mName.C_Str());
			light->LocalPosition(position.x * scale, position.y* scale, position.z* scale);
			light->Type(LIGHT_TYPE_POINT);
			light->Intensity(li * pointLightIntensity);
			light->Range((-b + sqrtf(b*b - 4*a*c)) / (2 * a));
			light->Color(col / li);
			AddObject(light);

			objectMap.at(lightNode->mParent)->AddChild(light.get());
			break;
		}
		}
	}

	printf("Loaded %s\n", filename.c_str());
	return root;
}

void Scene::Update(CommandBuffer* commandBuffer) {
	auto t1 = mClock.now();
	mDeltaTime = (t1 - mLastFrame).count() * 1e-9f;
	mTotalTime = (t1 - mStartTime).count() * 1e-9f;
	mLastFrame = t1;

	// count fps
	mFrameTimeAccum += mDeltaTime;
	mFpsAccum++;
	if (mFrameTimeAccum > 1.f) {
		mFps = mFpsAccum / mFrameTimeAccum;
		mFrameTimeAccum -= 1.f;
		mFpsAccum = 0;
	}

	PROFILER_BEGIN("FixedUpdate");
	float physicsTime = 0;
	mFixedAccumulator += mDeltaTime;
	t1 = mClock.now();
	while (mFixedAccumulator > mFixedTimeStep && physicsTime < mPhysicsTimeLimitPerFrame) {
		for (const auto& p : mPluginManager->Plugins())
			if (p->mEnabled) p->FixedUpdate(commandBuffer);
		for (auto o : mObjects)
			if (o->EnabledHierarchy()) o->FixedUpdate(commandBuffer);

		mFixedAccumulator -= mFixedTimeStep;
		physicsTime = (mClock.now() - t1).count() * 1e-9f;
	}
	PROFILER_END;

	PROFILER_BEGIN("Update");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled) p->PreUpdate(commandBuffer);
		for (auto o : mObjects)
			if (o->EnabledHierarchy()) o->PreUpdate(commandBuffer);

	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled) p->Update(commandBuffer);
	for (auto o : mObjects)
		if (o->EnabledHierarchy()) o->Update(commandBuffer);

	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled) p->PostUpdate(commandBuffer);
	for (auto o : mObjects)
		if (o->EnabledHierarchy()) o->PostUpdate(commandBuffer);
	PROFILER_END;

	sort(mCameras.begin(), mCameras.end(), [](const auto& a, const auto& b) { return a->RenderPriority() > b->RenderPriority(); });

	RenderShadows(commandBuffer, mCameras[0]);
}

void Scene::AddObject(shared_ptr<Object> object) {
	mObjects.push_back(object);
	object->mScene = this;

	if (auto l = dynamic_cast<Light*>(object.get()))
		mLights.push_back(l);
	if (auto c = dynamic_cast<Camera*>(object.get()))
		mCameras.push_back(c);
	if (auto r = dynamic_cast<Renderer*>(object.get()))
		mRenderers.push_back(r);

	mBvhDirty = true;
}
void Scene::RemoveObject(Object* object) {
	if (!object) return;

	if (auto l = dynamic_cast<Light*>(object))
		for (auto it = mLights.begin(); it != mLights.end();) {
			if (*it == l) {
				it = mLights.erase(it);
				break;
			} else
				it++;
		}

	if (auto c = dynamic_cast<Camera*>(object))
		for (auto it = mCameras.begin(); it != mCameras.end();) {
			if (*it == c) {
				it = mCameras.erase(it);
				break;
			} else
				it++;
		}

	if (auto r = dynamic_cast<Renderer*>(object))
		for (auto it = mRenderers.begin(); it != mRenderers.end();) {
			if (*it == r) {
				it = mRenderers.erase(it);
				break;
			} else
				it++;
		}

	for (auto it = mObjects.begin(); it != mObjects.end();)
		if (it->get() == object) {
			mBvhDirty = true;
			while (object->mChildren.size())
				object->RemoveChild(object->mChildren[0]);
			if (object->mParent) object->mParent->RemoveChild(object);
			object->mParent = nullptr;
			object->mScene = nullptr;
			it = mObjects.erase(it);
			break;
		} else
			it++;
}

void Scene::AddShadowCamera(uint32_t si, ShadowData* sd, bool ortho, float size, const float3& pos, const quaternion& rot, float near, float far) {
	if (mShadowCameras.size() <= si)
		mShadowCameras.push_back(new Camera("ShadowCamera", mShadowAtlas, CLEAR_NONE));
	Camera* sc = mShadowCameras[si];

	sc->Orthographic(ortho);
	if (ortho) sc->OrthographicSize(size);
	else sc->FieldOfView(size);
	sc->Near(near);
	sc->Far(far);
	sc->LocalPosition(pos);
	sc->LocalRotation(rot);

	VkViewport vp = sc->Viewport();
	vp.x = (float)((si % (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION)) * SHADOW_RESOLUTION);
	vp.y = (float)((si / (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION)) * SHADOW_RESOLUTION);
	vp.width = vp.height = SHADOW_RESOLUTION;
	sc->Viewport(vp);

	sd->WorldToShadow = sc->ViewProjection();
	sd->CameraPosition = pos;
	sd->ShadowST = float4(vp.width - 2, vp.height - 2, vp.x + 1, vp.y + 1) / SHADOW_ATLAS_RESOLUTION;
	sd->InvProj22 = 1.f / (sc->Projection()[2][2] * (far - near));
};

void Scene::RenderShadows(CommandBuffer* commandBuffer, Camera* camera) {
	mLightBuffer = commandBuffer->GetBuffer("Light Buffer", MAX_GPU_LIGHTS * sizeof(GPULight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
	mShadowBuffer = commandBuffer->GetBuffer("Shadow Buffer", MAX_GPU_LIGHTS * sizeof(ShadowData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	mShadowAtlas->PreBeginRenderPass();

	if (mLights.empty() || mRenderers.empty()) return;

	PROFILER_BEGIN("Lighting");
	uint32_t si = 0;
	mShadowCount = 0;
	mActiveLights.clear();
	
	AABB sceneBounds = BVH()->RendererBounds();
	float3 sceneCenter = sceneBounds.Center();
	float3 sceneExtent = sceneBounds.Extents();
	float sceneExtentMax = max(max(sceneExtent.x, sceneExtent.y), sceneExtent.z) * 1.73205080757f; // sqrt(3)*x
	
	// Create shadow cameras and GPULights

	PROFILER_BEGIN("Gather Lights");
	uint32_t li = 0;
	GPULight* lights = (GPULight*)LightBuffer()->MappedData();
	ShadowData* shadows = (ShadowData*)ShadowBuffer()->MappedData();

	uint32_t maxShadows = (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION) * (SHADOW_ATLAS_RESOLUTION / SHADOW_RESOLUTION);

	float tanfov = tanf(camera->FieldOfView() * .5f);
	float3 cp = camera->WorldPosition();
	float3 fwd = camera->WorldRotation() * float3(0, 0, 1);

	Ray rays[4] {
		camera->ScreenToWorldRay(float2(0, 0)),
		camera->ScreenToWorldRay(float2(1, 0)),
		camera->ScreenToWorldRay(float2(0, 1)),
		camera->ScreenToWorldRay(float2(1, 1))
	};
	float3 corners[8];

	for (Light* l : mLights) {
		if (!l->EnabledHierarchy()) continue;
		mActiveLights.push_back(l);

		float cosInner = cosf(l->InnerSpotAngle());
		float cosOuter = cosf(l->OuterSpotAngle());

		lights[li].WorldPosition = l->WorldPosition();
		lights[li].InvSqrRange = 1.f / (l->Range() * l->Range());
		lights[li].Color = l->Color() * l->Intensity();
		lights[li].SpotAngleScale = 1.f / fmaxf(.001f, cosInner - cosOuter);
		lights[li].SpotAngleOffset = -cosOuter * lights[li].SpotAngleScale;
		lights[li].Direction = -(l->WorldRotation() * float3(0, 0, 1));
		lights[li].Type = l->Type();
		lights[li].ShadowIndex = -1;
		lights[li].CascadeSplits = -1.f;

		if (l->CastShadows() && si+1 < maxShadows) {
			switch (l->Type()) {
			case LIGHT_TYPE_SUN: {
				float4 cascadeSplits = 0;
				float cf = min(l->ShadowDistance(), camera->Far());

				switch (l->CascadeCount()) {
				case 4:
					cascadeSplits[0] = cf * .07f;
					cascadeSplits[1] = cf * .18f;
					cascadeSplits[2] = cf * .40f;
					cascadeSplits[3] = cf;
				case 3:
					cascadeSplits[0] = cf * .15f;
					cascadeSplits[1] = cf * .4f;
					cascadeSplits[2] = cf;
					cascadeSplits[3] = cf;
				case 2:
					cascadeSplits[0] = cf * .4f;
					cascadeSplits[1] = cf;
					cascadeSplits[2] = cf;
					cascadeSplits[3] = cf;
				case 1:
					cascadeSplits = cf;
				}

				lights[li].CascadeSplits = cascadeSplits / camera->Far();
				lights[li].ShadowIndex = (int32_t)si;
				
				float z0 = camera->Near();
				for (uint32_t ci = 0; ci < l->CascadeCount(); ci++) {
					float z1 = cascadeSplits[ci];

					// compute corners and center of the frusum this cascade covers
					float3 pos = 0;
					for (uint32_t j = 0; j < 4; j++) {
						corners[j]   = rays[j].mOrigin + rays[j].mDirection * z0;
						corners[j+4] = rays[j].mOrigin + rays[j].mDirection * z1;
						pos += corners[j] + corners[j+4];
					}
					pos /= 8.f;

					// min and max relative to light rotation
					float3 mx = 0;
					float3 mn = 1e20f;
					quaternion r = inverse(l->WorldRotation());
					for (uint32_t j = 0; j < 8; j++) {
						float3 rc = r * (corners[j] - pos);
						mx = max(mx, rc);
						mn = min(mn, rc);
					}

					if (max(mx.x - mn.x, mx.y - mn.y) > sceneExtentMax) {
						// use scene bounds instead of frustum bounds
						pos = sceneCenter;
						corners[0] = float3(-sceneExtent.x,  sceneExtent.y, -sceneExtent.z) + sceneCenter;
						corners[1] = float3( sceneExtent.x,  sceneExtent.y, -sceneExtent.z) + sceneCenter;
						corners[2] = float3(-sceneExtent.x, -sceneExtent.y, -sceneExtent.z) + sceneCenter;
						corners[3] = float3( sceneExtent.x, -sceneExtent.y, -sceneExtent.z) + sceneCenter;
						corners[4] = float3(-sceneExtent.x,  sceneExtent.y,  sceneExtent.z) + sceneCenter;
						corners[5] = float3( sceneExtent.x,  sceneExtent.y,  sceneExtent.z) + sceneCenter;
						corners[6] = float3(-sceneExtent.x, -sceneExtent.y,  sceneExtent.z) + sceneCenter;
						corners[7] = float3( sceneExtent.x, -sceneExtent.y,  sceneExtent.z) + sceneCenter;
					}

					// project direction onto scene bounds for near and far
					float3 fwd   = l->WorldRotation() * float3(0, 0, 1);
					float3 right = l->WorldRotation() * float3(1, 0, 0);
					float near = 0;
					float far = 0;
					float sz = 0;
					for (uint32_t j = 0; j < 8; j++){
						float d = dot(corners[j] - pos, fwd);
						near = min(near, d);
						far = max(far, d);
						sz = max(sz, abs(dot(corners[j] - pos, right)));
					}

					AddShadowCamera(si, &shadows[si], true, 2*sz, pos, l->WorldRotation(), near, far);
					si++;
					z0 = z1;
				}

				break;
			}
			case LIGHT_TYPE_POINT:
				break;
			case LIGHT_TYPE_SPOT:
				lights[li].CascadeSplits = 1.f;
				lights[li].ShadowIndex = (int32_t)si;
				AddShadowCamera(si, &shadows[si], false, l->OuterSpotAngle() * 2, l->WorldPosition(), l->WorldRotation(), l->Radius() - .001f, l->Range());
				si++;
				break;
			}
		}

		li++;
		if (li >= MAX_GPU_LIGHTS) break;
	}
	PROFILER_END;
		
	if (si) {
		PROFILER_BEGIN("Render Shadows");
		BEGIN_CMD_REGION(commandBuffer, "Render Shadows");

		commandBuffer->TransitionBarrier(mShadowAtlas->DepthBuffer(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
		mShadowAtlas->Clear(commandBuffer, CLEAR_DEPTH);

		for (uint32_t i = 0; i < si; i++) {
			mShadowCameras[i]->mEnabled = true;
			Render(commandBuffer, mShadowCameras[i], PASS_DEPTH, i == 0);
			mShadowCount++;
		}
		for (uint32_t i = si; i < mShadowCameras.size(); i++)
			mShadowCameras[i]->mEnabled = false;
		
		commandBuffer->TransitionBarrier(mShadowAtlas->DepthBuffer(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		END_CMD_REGION(commandBuffer);
		PROFILER_END;
	}
}

void Scene::Render(CommandBuffer* commandBuffer, Camera* camera, PassType pass, bool clear) {
	PROFILER_BEGIN("Culling/Sorting");
	vector<Object*> objects;
	BVH()->FrustumCheck(camera->Frustum(), objects, pass);
	vector<Renderer*> renderers;
	renderers.reserve(objects.size());
	for (Object* o : objects) if (Renderer* r = dynamic_cast<Renderer*>(o)) renderers.push_back(r);
	sort(renderers.begin(), renderers.end(), RendererCompare);
	PROFILER_END;
	Render(commandBuffer, camera, pass, clear, renderers);
}

void Scene::Render(CommandBuffer* commandBuffer, Camera* camera, PassType pass, bool clear, vector<Renderer*>& renderers) {
	camera->PreBeginRenderPass();
	if (camera->Framebuffer()->Extent().width == 0 || camera->Framebuffer()->Extent().height == 0) return;

	PROFILER_BEGIN("PreBeginRenderPass");
	BEGIN_CMD_REGION(commandBuffer, "PreBeginRenderPass");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PreBeginRenderPass(commandBuffer, camera, pass);
	if (camera->ClearFlags() == CLEAR_SKYBOX && mSkyboxMaterial && pass == PASS_MAIN) {
		SetEnvironmentParameters(mSkyboxMaterial);
		mSkyboxMaterial->PreBeginRenderPass(commandBuffer, pass);
	}
	for (Renderer* r : renderers)
		r->PreBeginRenderPass(commandBuffer, camera, pass);
	END_CMD_REGION(commandBuffer);
	PROFILER_END;
	
	PROFILER_BEGIN("DrawGUI");
	GUI::Reset(commandBuffer);
	for (const auto& r : mObjects)
		if (r->EnabledHierarchy())
			r->DrawGUI(commandBuffer, camera);
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->DrawGUI(commandBuffer, camera);
	GUI::PreBeginRenderPass(commandBuffer);
	PROFILER_END;

	PROFILER_BEGIN("Render");
	BEGIN_CMD_REGION(commandBuffer, "Render");

	camera->BeginRenderPass(commandBuffer);

	PROFILER_BEGIN("PreRender");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled) p->PreRender(commandBuffer, camera, pass);
	PROFILER_END;

	// Skybox
	if (camera->ClearFlags() == CLEAR_SKYBOX && mSkyboxMaterial && pass == PASS_MAIN) {
		ShaderVariant* shader = mSkyboxMaterial->GetShader(PASS_MAIN);
		VkPipelineLayout layout = commandBuffer->BindMaterial(mSkyboxMaterial, pass, mSkyboxCube->VertexInput(), camera, mSkyboxCube->Topology());
		commandBuffer->BindVertexBuffer(mSkyboxCube->VertexBuffer().get(), 0, 0);
		commandBuffer->BindIndexBuffer(mSkyboxCube->IndexBuffer().get(), 0, mSkyboxCube->IndexType());
		camera->SetStereoViewport(commandBuffer, shader, EYE_LEFT);
		vkCmdDrawIndexed(*commandBuffer, mSkyboxCube->IndexCount(), 1, mSkyboxCube->BaseIndex(), mSkyboxCube->BaseVertex(), 0);
		commandBuffer->mTriangleCount += mSkyboxCube->IndexCount() / 3;
		if (camera->StereoMode() != STEREO_NONE) {
			camera->SetStereoViewport(commandBuffer, shader, EYE_RIGHT);
			vkCmdDrawIndexed(*commandBuffer, mSkyboxCube->IndexCount(), 1, mSkyboxCube->BaseIndex(), mSkyboxCube->BaseVertex(), 0);
			commandBuffer->mTriangleCount += mSkyboxCube->IndexCount() / 3;
		}
	}

	Buffer* instanceBuffer = nullptr;
	uint32_t instanceCount = 0;
	Renderer* firstInstance = nullptr;
	bool drawGui = true;

	for (Object* o : renderers)
		if (Renderer* r = dynamic_cast<Renderer*>(o)) {
			if (!r || !r->Visible()) continue;

			if (drawGui && r->RenderQueue() > GUI::mRenderQueue) {
				GUI::Draw(commandBuffer, pass, camera);
				drawGui = false;
			}

			// TODO: call TryCombineInstances outside of RenderPass for potential compute support
			if (!firstInstance || firstInstance->TryCombineInstances(commandBuffer, r, instanceBuffer, instanceCount)) {
				// Failed to combine, draw last batch
				firstInstance->DrawInstanced(commandBuffer, camera, pass, instanceBuffer, instanceCount);
				instanceCount = 0;
				firstInstance = nullptr;
				r->Draw(commandBuffer, camera, pass);
			}
		}
	if (firstInstance) firstInstance->DrawInstanced(commandBuffer, camera, pass, instanceBuffer, instanceCount);
	if (drawGui) GUI::Draw(commandBuffer, pass, camera);

	camera->SetViewportScissor(commandBuffer);

	PROFILER_BEGIN("PostRender");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled) p->PostRender(commandBuffer, camera, pass);
	PROFILER_END;

	vkCmdEndRenderPass(*commandBuffer);

	PROFILER_BEGIN("PostEndRenderPass");
	BEGIN_CMD_REGION(commandBuffer, "PostEndRenderPass");
	for (const auto& p : mPluginManager->Plugins())
		if (p->mEnabled)
			p->PostEndRenderPass(commandBuffer, camera, pass);
	END_CMD_REGION(commandBuffer);
	PROFILER_END;

	END_CMD_REGION(commandBuffer);
	PROFILER_END;
}

void Scene::SetEnvironmentParameters(Material* mat) {
	mat->SetPushParameter("AmbientLight", mAmbientLight);
	mat->SetPushParameter("Time", mTotalTime);
	mat->SetPushParameter("LightCount", (uint32_t)mActiveLights.size());
	mat->SetPushParameter("ShadowTexelSize", float2(1.f / mShadowAtlas->Extent().width, 1.f / mShadowAtlas->Extent().height));

	mat->SetStorageBuffer("Lights", mLightBuffer);
	mat->SetStorageBuffer("Shadows", mShadowBuffer);
	mat->SetSampledTexture("ShadowAtlas", mShadowAtlas->DepthBuffer());
	mat->SetSampledTexture("EnvironmentTexture", mEnvironmentTexture);
}

vector<Object*> Scene::Objects() const {
	vector<Object*> objs(mObjects.size());
	for (uint32_t i = 0; i < mObjects.size(); i++)
		objs[i] = mObjects[i].get();
	return objs;
}

ObjectBvh2* Scene::BVH() {
	if (mBvhDirty) {
		PROFILER_BEGIN("Build BVH");
		vector<Object*> objs = Objects();
		mBvh->Build(objs.data(), objs.size());
		mBvhDirty = false;
		mLastBvhBuild = mInstance->Device()->FrameCount();
		PROFILER_END;
	}
	return mBvh;
}