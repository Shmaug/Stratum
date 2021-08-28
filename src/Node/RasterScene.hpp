#pragma once

#include "DynamicRenderPass.hpp"
#include <Core/PipelineState.hpp>

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
	component_ptr<PipelineState> mGeometryPipeline;
	component_ptr<PipelineState> mBackgroundPipeline;
	Texture::View mBlankTexture;

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
	struct MeshInstance {
		Mesh mMesh;
		uint32_t mIndexCount;
		uint32_t mFirstVertex;
		uint32_t mFirstIndex;
		uint32_t mMaterialIndex;
		uint32_t mInstanceIndex;
	};
	struct Gizmos {
	public:
		Gizmos() = default;
		Gizmos(const Gizmos&) = default;
		Gizmos(Gizmos&&) = default;
		inline Gizmos(Device& device, RasterScene& scene, const component_ptr<PipelineState>& pipeline) : mPipeline(pipeline), mVertices(device), mIndices(device) {}

		STRATUM_API void cube  (const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones());
		STRATUM_API void sphere(const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones());
		STRATUM_API void quad  (const hlsl::TransformData& transform, const hlsl::float4& color = hlsl::float4::Ones(), const Texture::View& texture = {}, const hlsl::float4& textureST = hlsl::float4(1,1,0,0));

		STRATUM_API void draw(CommandBuffer& commandBuffer) const;

	private:
		friend class RasterScene;
		struct vertex_t {
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
			uint32_t mTextureIndex;
			vk::PrimitiveTopology mTopology;
		};
		vector<Gizmo> mGizmos;
		component_ptr<PipelineState> mPipeline;
		unordered_map<Texture::View, uint32_t> mTextures;
		buffer_vector<vertex_t> mVertices;
		buffer_vector<uint16_t> mIndices;

		shared_ptr<Geometry> mDrawGeometry;
		Buffer::StrideView mDrawIndices;

		STRATUM_API void clear();
		STRATUM_API void pre_render(CommandBuffer& commandBuffer, RasterScene& scene);
	};

	NodeEvent<Gizmos&> OnGizmos;

	STRATUM_API RasterScene(Node& node);

	inline Node& node() const { return mNode; }
	inline DynamicRenderPass& shadow_node() const { return *mShadowNode; }
	inline const auto& background() const { return mBackgroundPipeline; }

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading = true) const;

private:
	struct DrawData {
	private:
		component_ptr<PipelineState> mGeometryPipeline;
		component_ptr<PipelineState> mBackgroundPipeline;
		unordered_map<size_t, unordered_map<uint32_t, list<component_ptr<MeshInstance>>>> mMeshInstances;
		unique_ptr<Gizmos> mGizmoData;

	public:
		DrawData() = default;
		DrawData(const DrawData&) = default;
		DrawData(DrawData&&) = default;
		inline DrawData(RasterScene& scene) : mGeometryPipeline(scene.mGeometryPipeline) {}
		
		inline void add_instance(const component_ptr<MeshInstance>& instance) {
			mMeshInstances[hash_args(instance->mMesh.description(*mGeometryPipeline->stage(vk::ShaderStageFlagBits::eVertex)), instance->mMesh.indices().stride(), instance->mMesh.topology())][instance->mMaterialIndex].emplace_back(instance);
		}

		STRATUM_API void clear();
		STRATUM_API void draw(CommandBuffer& commandBuffer, bool doShading) const;
	};

	unique_ptr<Gizmos> mGizmos;
	unique_ptr<DrawData> mDrawData;

	friend struct Gizmos;
	friend struct DrawData;
};

}