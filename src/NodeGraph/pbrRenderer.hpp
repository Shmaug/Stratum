#pragma once

#include "RenderNode.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include "../Shaders/pbr.hlsl"
#pragma pack(pop)
}

class pbrRenderer {
private:
	NodeGraph& mNodeGraph;
	shared_ptr<Material> mMaterial;
	shared_ptr<Material> mShadowMaterial;
	RenderNode* mRenderNode;
	RenderNode* mShadowRenderNode;

	STRATUM_API void pre_render(CommandBuffer& commandBuffer) const;

public:
	struct PrimitiveSet {
		Geometry mGeometry;
		Buffer::StrideView mIndices;
		hlsl::MaterialData mMaterialData;

		uint32_t mIndexCount;
		uint32_t mVertexOffset;
		uint32_t mFirstIndex;
		
		uint32_t mMaterialIndex;
		uint32_t mBaseColorTexture;
		uint32_t mNormalTexture;
		uint32_t mMetallicRoughnessTexture;
		uint32_t mOcclusionTexture;
		uint32_t mEmissionTexture;
	};

	STRATUM_API pbrRenderer(NodeGraph& nodeGraph, Device& device, const shared_ptr<SpirvModule>& vs, const shared_ptr<SpirvModule>& fs,
		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4, vk::ImageLayout dstLayout = vk::ImageLayout::ePresentSrcKHR);
	
	inline NodeGraph& node_graph() const { return mNodeGraph; }
	inline RenderNode& main_pass() const { return *mRenderNode; }
	inline RenderNode& shadow_pass() const { return *mShadowRenderNode; }
	inline Material& material() const { return *mMaterial; }
	inline Material& shadow_material() const { return *mShadowMaterial; }

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);

	inline void render(CommandBuffer& commandBuffer, const Texture::View& renderTarget, const hlsl::TransformData& cameraToWorld, const hlsl::ProjectionData& projection) const {
		mMaterial->push_constant("Projection", projection);
		mMaterial->push_constant("WorldToCamera", inverse(cameraToWorld));
		main_pass().render(commandBuffer, {
			{ "primaryResolve", renderTarget }
		});
	}
};

}