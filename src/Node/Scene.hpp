#pragma once

#include <nanovdb/util/GridHandle.h>
#include <imgui/imgui.h> // materials have ImGui calls

#include <Common/hlsl_compat.hpp>
#include <Core/PipelineState.hpp>

#include "NodeGraph.hpp"

namespace stm {

#pragma pack(push)
#pragma pack(1)
namespace hlsl {
#include <HLSL/scene.hlsli>
#include <HLSL/material.hlsli>
}
#pragma pack(pop)

struct Camera {
	hlsl::ProjectionData mProjection;
	vk::Rect2D mImageRect;

	inline hlsl::ViewData view(const hlsl::TransformData& to_world) {
		hlsl::ViewData v;
		v.camera_to_world = to_world;
		v.world_to_camera = to_world.inverse();
		v.projection = mProjection;
		v.image_min = { mImageRect.offset.x, mImageRect.offset.y };
		v.image_max = { mImageRect.offset.x + mImageRect.extent.width, mImageRect.offset.y + mImageRect.extent.height };
		hlsl::float2 extent = mProjection.back_project(hlsl::float2::Constant(1)).head<2>() - mProjection.back_project(hlsl::float2::Constant(-1)).head<2>();
		if (!mProjection.orthographic) extent /= mProjection.near_plane;
		v.projection.sensor_area = abs(extent[0] * extent[1]);
		return v;
	}
};

struct MeshPrimitive {
	component_ptr<hlsl::Material> mMaterial;
	component_ptr<Mesh> mMesh;
};
struct SpherePrimitive {
	component_ptr<hlsl::Material> mMaterial;
	float mRadius;
};

STRATUM_API hlsl::TransformData node_to_world(const Node& node);

STRATUM_API Mesh load_serialized(CommandBuffer& commandBuffer, const fs::path& filename, int shape_idx = -1);
STRATUM_API Mesh load_obj(CommandBuffer& commandBuffer, const fs::path& filename);

STRATUM_API void load_environment_map(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_mitsuba(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#ifdef STRATUM_ENABLE_ASSIMP
STRATUM_API void load_assimp(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#endif
STRATUM_API void load_vol(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_nvdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#ifdef STRATUM_ENABLE_OPENVDB
STRATUM_API void load_vdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
#endif

inline unordered_map<string, function<void(Node&, CommandBuffer&, const fs::path&)>> scene_loader_map() {
	return {
		{ ".hdr", &load_environment_map },
		{ ".exr", &load_environment_map },
		{ ".xml", &load_mitsuba },
		{ ".gltf", &load_gltf },
		{ ".glb", &load_gltf },

#ifdef STRATUM_ENABLE_ASSIMP
		{ ".fbx", &load_assimp },
		{ ".obj", &load_assimp },
		{ ".blend", &load_assimp },
		{ ".ply", &load_assimp },
		{ ".stl", &load_assimp },
#endif
		{ ".vol", &load_vol },
		{ ".nvdb", &load_nvdb },
#ifdef STRATUM_ENABLE_OPENVDB
		{ ".vdb", &load_vdb }
#endif
	};
}
inline vector<string> scene_loader_filters() {
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
inline void load_scene(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
	const auto& m = scene_loader_map();
	auto it = m.find(filename.extension().string());
	if (it != m.end()) {
		it->second(root, commandBuffer, filename);
	}
}

}