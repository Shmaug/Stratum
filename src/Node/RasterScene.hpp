#pragma once

#include "Scene.hpp"

namespace stm {

class RasterScene {
public:
	STRATUM_API RasterScene(Node& node);
	STRATUM_API void create_pipelines();

	inline Node& node() const { return mNode; }
	inline DynamicRenderPass& shadow_node() const { return *mShadowPass; }
		
	STRATUM_API void update(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading = true) const;

private:
	struct DrawCall {
		Mesh* mMesh;
		uint32_t mMaterialIndex;
		uint32_t mFirstIndex;
		uint32_t mIndexCount;
		uint32_t mFirstInstance;
		uint32_t mInstanceCount;
	};
	Node& mNode;
	component_ptr<DynamicRenderPass> mShadowPass;
	component_ptr<GraphicsPipelineState> mGeometryPipeline;
	component_ptr<GraphicsPipelineState> mBackgroundPipeline;
	vector<DrawCall> mDrawCalls;
};

}