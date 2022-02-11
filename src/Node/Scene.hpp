#pragma once

#include <Common/hlsl_compat.hpp>
#include <Core/CommandBuffer.hpp>

#include "Gui.hpp"

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

}