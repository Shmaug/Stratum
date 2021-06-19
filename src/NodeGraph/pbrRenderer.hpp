#pragma once

#include "RenderGraph.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include "../Shaders/pbr.hlsl"
#pragma pack(pop)
}

class PbrRenderer {
private:
	NodeGraph::Node& mNode;
	RenderGraph& mShadowPass;
	shared_ptr<Material> mMaterial;
	shared_ptr<Material> mShadowMaterial;
	
public:
	struct PrimitiveSet {
		Geometry mGeometry;
		Buffer::StrideView mIndices;

		uint32_t mIndexCount;
		uint32_t mVertexOffset;
		uint32_t mFirstIndex;
		
		uint32_t mMaterialIndex;
		hlsl::TextureIndices mTextureIndices;
	};

	STRATUM_API PbrRenderer(NodeGraph::Node& node);

	inline NodeGraph::Node& node() { return mNode; }
	inline RenderGraph& shadow_pass() const { return mShadowPass; }
	inline Material& material() const { return *mMaterial; }
	inline Material& shadow_material() const { return *mShadowMaterial; }

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);

	STRATUM_API void pre_render(CommandBuffer& commandBuffer) const;

	inline void draw(Material& material, CommandBuffer& commandBuffer) const {
		mNode.for_each_child<PrimitiveSet>([&](const PrimitiveSet& primitive) {
			material.bind(commandBuffer, primitive.mGeometry);
			material.bind_descriptor_sets(commandBuffer);
			material.push_constants(commandBuffer);
			commandBuffer.push_constant("gMaterialIndex", primitive.mMaterialIndex);
			primitive.mGeometry.drawIndexed(commandBuffer, primitive.mIndices, primitive.mIndexCount, 1, primitive.mFirstIndex, primitive.mVertexOffset, primitive.mMaterialIndex);
		});
	}

};

}