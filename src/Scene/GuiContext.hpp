#pragma once

#include "RenderNode.hpp"

namespace stm {

class GuiNode : public Scene::Node {
private:
	shared_ptr<Mesh> mMesh;
	TextureView mFonts;
	shared_ptr<GraphicsPipeline> mPipeline;

public:
	STRATUM_API GuiNode(CommandBuffer& commandBuffer);
	STRATUM_API void OnDraw(CommandBuffer& commandBuffer);
};

}