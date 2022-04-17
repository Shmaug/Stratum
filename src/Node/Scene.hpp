#pragma once

#include <nanovdb/util/GridHandle.h>

#include <Common/hlsl_compat.hpp>
#include <Core/PipelineState.hpp>

#include "NodeGraph.hpp"
#include <imgui.h> // materials have ImGui calls

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
STRATUM_API void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_mitsuba(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_vol(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_vdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_nvdb(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);
STRATUM_API void load_assimp(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);

}