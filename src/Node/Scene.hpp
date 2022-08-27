#pragma once

#include <nanovdb/util/GridHandle.h>
#include <imgui/imgui.h> // materials have ImGui calls

#include <Core/AccelerationStructure.hpp>
#include "NodeGraph.hpp"

namespace stm {

#pragma pack(push)
#pragma pack(1)
#include <Shaders/scene.h>
#include <Shaders/material.h>
#pragma pack(pop)

// Note: Scene provides inspector gui callbacks for these classes and ones in scene.h

struct Camera {
	ProjectionData mProjection;
	vk::Rect2D mImageRect;

	inline ViewData view() {
		ViewData v;
		v.projection = mProjection;
		v.image_min = { mImageRect.offset.x, mImageRect.offset.y };
		v.image_max = { mImageRect.offset.x + mImageRect.extent.width, mImageRect.offset.y + mImageRect.extent.height };
		float2 extent = mProjection.back_project(float2::Constant(1)).head<2>() - mProjection.back_project(float2::Constant(-1)).head<2>();
		if (!mProjection.orthographic) extent /= mProjection.near_plane;
		v.projection.sensor_area = abs(extent[0] * extent[1]);
		return v;
	}
};
struct MeshPrimitive {
	component_ptr<Material> mMaterial;
	component_ptr<Mesh> mMesh;
};
struct SpherePrimitive {
	component_ptr<Material> mMaterial;
	float mRadius;
};

STRATUM_API TransformData node_to_world(const Node& node);

STRATUM_API Mesh load_serialized(CommandBuffer& commandBuffer, const fs::path& filename, int shape_idx = -1);
STRATUM_API Mesh load_obj(CommandBuffer& commandBuffer, const fs::path& filename);

class Scene {
public:
	struct SceneData {
		MaterialResources mResources;

		shared_ptr<AccelerationStructure> mScene;
		unordered_map<const void* /* address of component */, pair<TransformData, uint32_t /* instance index */ >> mInstanceTransformMap;
		vector<Node*> mInstanceNodes;

		Buffer::View<PackedVertexData> mVertices;
		Buffer::View<byte> mIndices;
		Buffer::View<byte> mMaterialData;
		Buffer::View<InstanceData> mInstances;
		Buffer::View<TransformData> mInstanceTransforms;
		Buffer::View<TransformData> mInstanceInverseTransforms;
		Buffer::View<TransformData> mInstanceMotionTransforms;
		Buffer::View<uint32_t> mLightInstances;
		Buffer::View<float> mDistributionData;
		Buffer::View<uint32_t> mInstanceIndexMap;

		uint32_t mEnvironmentMaterialAddress;
		uint32_t mMaterialCount;
		uint32_t mLightDistributionPDF;
		uint32_t mLightDistributionCDF;
		uint32_t mEmissivePrimitiveCount;
	};

	STRATUM_API Scene(Node& node);

	STRATUM_API void create_pipelines();

	inline Node& node() const { return mNode; }

	inline const shared_ptr<SceneData>& data() const { return mSceneData; }

	STRATUM_API void on_inspector_gui();

	STRATUM_API void update(CommandBuffer& commandBuffer, const float deltaTime);

	STRATUM_API void load_environment_map(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void load_mitsuba(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void load_vol(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void load_nvdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#ifdef STRATUM_ENABLE_ASSIMP
	STRATUM_API void load_assimp(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#endif
#ifdef STRATUM_ENABLE_OPENVDB
	STRATUM_API void load_vdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#endif

	inline vector<string> loader_filters() {
		return {
			"All Files", "*",
			"Environment Maps (.exr .hdr)", "*.exr *.hdr",
			"Mitsuba Scenes (.xml)", "*.xml",
			"glTF Scenes (.gltf .glb)", "*.gltf *.glb",
			"Mitsuba Volumes (.vol)" , "*.vol",
	#ifdef STRATUM_ENABLE_ASSIMP
			"Autodesk (.fbx)", "*.fbx",
			"Wavefront Object Files (.obj)", "*.obj",
			"Stanford Polygon Library Files (.ply)", "*.ply",
			"Stereolithography Files (.stl)", "*.stl",
			"Blender Scenes (.blend)", "*.blend",
	#endif
			"NVDB Volume (.nvdb)" , "*.nvdb",
	#ifdef STRATUM_ENABLE_OPENVDB
			"VDB Volumes (.vdb)", "*.vdb",
	#endif
		};
	}
	inline void load(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
		const string& ext = filename.extension().string();
		if (ext == ".hdr") load_environment_map(root, commandBuffer, filename);
		else if (ext == ".exr") load_environment_map(root, commandBuffer, filename);
		else if (ext == ".xml") load_mitsuba(root, commandBuffer, filename);
		else if (ext == ".gltf") load_gltf(root, commandBuffer, filename);
		else if (ext == ".glb") load_gltf(root, commandBuffer, filename);
		else if (ext == ".vol") load_vol(root, commandBuffer, filename);
		else if (ext == ".nvdb") load_nvdb(root, commandBuffer, filename);
	#ifdef STRATUM_ENABLE_ASSIMP
		else if (ext == ".fbx") load_assimp(root, commandBuffer, filename);
		else if (ext == ".obj") load_assimp(root, commandBuffer, filename);
		else if (ext == ".blend") load_assimp(root, commandBuffer, filename);
		else if (ext == ".ply") load_assimp(root, commandBuffer, filename);
		else if (ext == ".stl") load_assimp(root, commandBuffer, filename);
	#endif
	#ifdef STRATUM_ENABLE_OPENVDB
		else if (ext == ".vdb") load_vdb(root, commandBuffer, filename)
	#endif
		else
			throw runtime_error("unknown extension:" + ext);
	}

	STRATUM_API ImageValue1 alpha_to_roughness(CommandBuffer& commandBuffer, const ImageValue1& alpha);
	STRATUM_API ImageValue1 shininess_to_roughness(CommandBuffer& commandBuffer, const ImageValue1& alpha);
	STRATUM_API Material make_metallic_roughness_material(CommandBuffer& commandBuffer, const ImageValue3& base_color, const ImageValue4& metallic_roughness, const ImageValue3& transmission, const float eta, const ImageValue3& emission);
	STRATUM_API Material make_diffuse_specular_material(CommandBuffer& commandBuffer, const ImageValue3& diffuse, const ImageValue3& specular, const ImageValue1& roughness, const ImageValue3& transmission, const float eta, const ImageValue3& emission);

private:
	Node& mNode;

	struct MeshAS {
		shared_ptr<AccelerationStructure> mAccelerationStructure;
		Buffer::StrideView mIndices;
	};

	unordered_map<size_t, shared_ptr<AccelerationStructure>> mAABBs;

	unordered_map<Mesh*, Buffer::View<PackedVertexData>> mMeshVertices;
	unordered_map<Mesh*, MeshAS> mMeshAccelerationStructures;

	shared_ptr<SceneData> mSceneData;

	shared_ptr<ComputePipelineState> mCopyVerticesPipeline;

	shared_ptr<ComputePipelineState> mConvertAlphaToRoughnessPipeline;
	shared_ptr<ComputePipelineState> mConvertShininessToRoughnessPipeline;
	shared_ptr<ComputePipelineState> mConvertPbrPipeline;
	shared_ptr<ComputePipelineState> mConvertDiffuseSpecularPipeline;

	vector<string> mToLoad;

	bool mAlwaysUpdate = false;
	bool mUpdateOnce = false;
};

}