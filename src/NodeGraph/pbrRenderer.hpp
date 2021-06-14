#pragma once

#include "RenderNode.hpp"

namespace stm {

namespace hlsl {
#pragma pack(push)
#pragma pack(1)
#include "../Shaders/pbr.hlsl"
#pragma pack(pop)
}

class PbrRenderer {
private:
	NodeGraph& mNodeGraph;
	shared_ptr<Material> mMaterial;
	shared_ptr<Material> mShadowMaterial;
	RenderNode* mShadowPass;


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

	STRATUM_API PbrRenderer(NodeGraph& nodeGraph, const shared_ptr<SpirvModule>& vs, const shared_ptr<SpirvModule>& fs);
	
	inline NodeGraph& node_graph() const { return mNodeGraph; }
	inline RenderNode& shadow_pass() const { return *mShadowPass; }
	inline Material& material() const { return *mMaterial; }
	inline Material& shadow_material() const { return *mShadowMaterial; }

	STRATUM_API void load_gltf(CommandBuffer& commandBuffer, const fs::path& filename);

	STRATUM_API void pre_render(CommandBuffer& commandBuffer) const;
	STRATUM_API void draw(CommandBuffer& commandBuffer, Material& material) const;
};

}