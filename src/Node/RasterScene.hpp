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
		float mNear;
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
	
	struct PushGeometry {
	public:
		#pragma pack(push)
		#pragma pack(1)
		struct vertex_t {
			hlsl::float3 mPosition;
			hlsl::float4 mColor;
			hlsl::float2 mTexcoord;
		};
		#pragma pack(pop)

		PushGeometry() = default;
		PushGeometry(const PushGeometry&) = default;
		PushGeometry(PushGeometry&&) = default;
		inline PushGeometry(Device& device, RasterScene& scene, const component_ptr<PipelineState>& pipeline) : mPipeline(pipeline),
			mVertices(device,0,vk::BufferUsageFlagBits::eVertexBuffer),
			mIndices(device,0,vk::BufferUsageFlagBits::eIndexBuffer), 
			mBlankTexture(scene.mBlankTexture) { clear(); }

		STRATUM_API uint32_t texture_index(const Texture::View& texture);
		
		STRATUM_API void sphere(const hlsl::float3& position, float size, const hlsl::float4& color = hlsl::float4::Ones(), const Texture::View& texture = {}, const hlsl::float4& textureST = hlsl::float4(1,1,0,0));
		STRATUM_API void quad  (const hlsl::float3& position, float rotation, const hlsl::float2& size, const hlsl::float4& color = hlsl::float4::Ones(), const Texture::View& texture = {}, const hlsl::float4& textureST = hlsl::float4(1,1,0,0));
		
		template<ranges::sized_range Vertices, ranges::sized_range Indices> requires(same_as<ranges::range_value_t<Vertices>, vertex_t> && integral<ranges::range_value_t<Indices>>)
		inline void geometry(const Vertices& vertices, const Indices& indices, vk::PrimitiveTopology topology, const hlsl::float3& transform, const hlsl::float4& color = hlsl::float4::Ones(), Texture::View texture = {}, const hlsl::float4& textureST = hlsl::float4(1,1,0,0)) {
			Instance& instance = mInstances.emplace_back((uint32_t)ranges::size(indices), (uint32_t)mVertices.size(), (uint32_t)mIndices.size(), topology, transform, color, textureST, texture_index(texture?texture:mBlankTexture));
			mVertices.resize(mVertices.size() + ranges::size(vertices));
			ranges::copy(vertices, mVertices.begin() + instance.mFirstVertex);
			mIndices.resize(offset + ranges::size(indices));
			ranges::copy(indices, mIndices.begin() + instance.mFirstIndex);
		}

	private:
		struct Instance {
			uint32_t mIndexCount;
			uint32_t mFirstVertex;
			uint32_t mFirstIndex;
			vk::PrimitiveTopology mTopology;
			hlsl::TransformData mTransform;
			hlsl::float4 mColor;
			hlsl::float4 mTextureST;
			uint32_t mTextureIndex;
		};

		component_ptr<PipelineState> mPipeline;
		unordered_map<Texture::View, uint32_t> mTextures;
		Texture::View mBlankTexture;
		buffer_vector<vertex_t> mVertices;
		buffer_vector<uint16_t> mIndices;
		vector<Instance> mInstances;

		pair<shared_ptr<Geometry>, Buffer::StrideView> mDrawData;

		friend class RasterScene;
		STRATUM_API void clear();
		STRATUM_API void pre_render(CommandBuffer& commandBuffer);
		STRATUM_API void draw(CommandBuffer& commandBuffer, const hlsl::TransformData& worldToCamera, const hlsl::ProjectionData& projection) const;
	};

	STRATUM_API RasterScene(Node& node);

	inline Node& node() const { return mNode; }
	inline DynamicRenderPass& shadow_node() const { return *mShadowNode; }
	inline const auto& background() const { return mBackgroundPipeline; }
	inline PushGeometry& push_geometry() { return *mPushGeometry; }
	inline void main_camera(const component_ptr<Camera>& c) { mMainCamera = c; }
	inline const component_ptr<Camera>& main_camera() const { return mMainCamera; }

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);
	STRATUM_API void pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading = true) const;

private:
	struct DrawData {
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

	private:
		component_ptr<PipelineState> mGeometryPipeline;
		component_ptr<PipelineState> mBackgroundPipeline;
		unordered_map<size_t, unordered_map<uint32_t, list<component_ptr<MeshInstance>>>> mMeshInstances;
		vector<reference_wrapper<PushGeometry>> mPushInstances;
	};
	friend struct PushGeometry;
	friend struct DrawData;

	unique_ptr<PushGeometry> mPushGeometry;
	unique_ptr<DrawData> mDrawData;
	component_ptr<Camera> mMainCamera;
};

}