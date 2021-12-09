#pragma once

#include <Core/CommandBuffer.hpp>

#include "NodeGraph.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include <HLSL/scene.hlsli>
#pragma pack(pop)
}


struct Camera {
	enum ProjectionMode {
		eOrthographic,
		ePerspective
	};

	ProjectionMode mProjectionMode;
	float mNear; // set below 0 for right handed
	float mFar; // set below 0 for right handed
	union {
		float mVerticalFoV;
		float mOrthographicHeight;
	};

	inline hlsl::ProjectionData projection(float aspect) const {
		return (mProjectionMode == ProjectionMode::eOrthographic) ?
			hlsl::make_orthographic(hlsl::float2(aspect, 1)*mOrthographicHeight, hlsl::float2::Zero(), mNear, mFar) :
			hlsl::make_perspective(mVerticalFoV, aspect, hlsl::float2::Zero(), mNear, mFar);
	}
};

struct EnvironmentMap {
	Image::View mImage;
	Image::View mMarginalDistribution;
	Image::View mConditionalDistribution;
	float mExposure;
	float mGamma;

	STRATUM_API static void build_distributions(const span<hlsl::float4>& img, const vk::Extent2D& extent, span<hlsl::float2> marginalDistData/*height,1*/, span<hlsl::float2> conditionalDistData /*width,height*/);
};

struct MaterialInfo {
  hlsl::float3 mAlbedo;
  hlsl::float3 mEmission;
	hlsl::float3 mAbsorption;
	float mMetallic;
  float mRoughness;
	float mTransmission;
	float mIndexOfRefraction;
	float mNormalScale; // scaledNormal = normalize((<sampled normal image value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))
	float mOcclusionScale; // lerp(color, color * <sampled occlusion image value>, <occlusion strength>)
	Image::View mAlbedoImage;
	Image::View mNormalImage;
	Image::View mEmissionImage;
	Image::View mMetallicImage;
	Image::View mRoughnessImage;
	Image::View mOcclusionImage;
	uint32_t mMetallicImageComponent;
	uint32_t mRoughnessImageComponent;
	uint32_t mOcclusionImageComponent;
	float mAlphaCutoff;
};

struct MeshPrimitive {
	component_ptr<MaterialInfo> mMaterial;
	component_ptr<Mesh> mMesh;
};

STRATUM_API hlsl::TransformData node_to_world(const Node& node);
STRATUM_API void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename);

}