#pragma once

#include <Core/PipelineState.hpp>
#include "Scene.hpp"

namespace stm {

class RasterScene {
public:
	STRATUM_API RasterScene(Node& node);
	STRATUM_API void create_pipelines();

	inline Node& node() const { return mNode; }
		
	STRATUM_API void update(CommandBuffer& commandBuffer);
	STRATUM_API void render(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const vk::Rect2D& renderArea, bool doShading = true) const;

private:
	struct DrawCall {
		Mesh* mMesh;
		uint32_t mMaterialIndex;
		uint32_t mFirstInstance;
		uint32_t mInstanceCount;
	};
	Node& mNode;
	shared_ptr<RenderPass> mShadowPass;
	component_ptr<GraphicsPipelineState> mGeometryPipeline;
	component_ptr<GraphicsPipelineState> mBackgroundPipeline;
	vector<DrawCall> mDrawCalls;
};

}