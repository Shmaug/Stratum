#pragma once

#include <imgui/imgui.h>
#include "RenderNode.hpp"

namespace stm {

class GuiContext : public Scene::Node {
private:
	shared_ptr<Mesh> mMesh;
	TextureView mFonts;
	shared_ptr<GraphicsPipeline> mPipeline;

public:
	STRATUM_API GuiContext(CommandBuffer& commandBuffer);
	STRATUM_API void Draw(CommandBuffer& commandBuffer);
};

}