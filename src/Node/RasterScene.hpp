#pragma once

#include "DynamicRenderPass.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include <HLSL/pbr.hlsli>
#pragma pack(pop)
}

class RasterScene {
private:
	Node& mNode;
	component_ptr<DynamicRenderPass> mShadowNode;
	shared_ptr<PipelineState> mGeometryPipeline;
	shared_ptr<PipelineState> mSkyboxPipeline;
	float mSkyboxGamma;
	shared_ptr<PipelineState> mGizmoPipeline;
	
	struct gizmo_vertex_t {
		hlsl::float3 mPosition;
		hlsl::float4 mColor;
		hlsl::float2 mTexcoord;
	};
	struct Gizmo {
		uint32_t mIndexCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
		hlsl::float4 mColor;
		hlsl::float4 mTextureST;
		Texture::View mTexture;
	};
	buffer_vector<gizmo_vertex_t> mGizmoVertices;
	buffer_vector<uint16_t> mGizmoIndices;
	vector<Gizmo> mGizmos;

public:
	STRATUM_API static hlsl::TransformData node_to_world(const Node& node);
	
	struct Camera {
		uint32_t mProjectionMode;
		float mFar;
		union {
			float mVerticalFoV;
			float mOrthographicHeight;
		};
	};
	struct Submesh {
		shared_ptr<Geometry> mGeometry;
		Buffer::StrideView mIndices;
		uint32_t mIndexCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
		uint32_t mMaterialIndex;
		uint32_t mInstanceIndex;
	};
	
	STRATUM_API RasterScene(Node& node);

	inline Node& node() { return mNode; }
	inline DynamicRenderPass& shadow_node() const { return *mShadowNode; }
	inline PipelineState& skybox_pipeline() const { return *mSkyboxPipeline; }
	inline PipelineState& geometry_pipeline() const { return *mGeometryPipeline; }
	inline void set_skybox(const shared_ptr<Texture>& skybox, float gamma = 2.2f) const {
		mSkyboxPipeline->descriptor("gTextures") = sampled_texture_descriptor(skybox);
		mSkyboxPipeline->push_constant("gEnvironmentGamma", gamma);
	}

	STRATUM_API void gizmo_cube  (const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones());
	STRATUM_API void gizmo_sphere(const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones());
	STRATUM_API void gizmo_quad  (const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones(), const Texture::View& texture = {}, const hlsl::float4& textureST = hlsl::float4(1,1,0,0));

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer, const component_ptr<Camera>& camera) const;
	STRATUM_API void draw(CommandBuffer& commandBuffer, vk::ShaderStageFlags stageMask = vk::ShaderStageFlagBits::eAll) const;
};

}