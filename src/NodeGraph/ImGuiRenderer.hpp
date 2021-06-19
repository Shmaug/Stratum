#pragma once

#include "NodeGraph.hpp"
#include "Material.hpp"

#include <imgui.h>

namespace stm {

class ImGuiRenderer {
private:
	NodeGraph::Node& mNode;
	shared_ptr<Material> mMaterial;
	Buffer::View<ImDrawVert> mVertices;
	Buffer::View<ImDrawIdx>  mIndices;
	const ImDrawData* mDrawData;
	
public:
	STRATUM_API ImGuiRenderer(NodeGraph::Node& node);
	STRATUM_API ~ImGuiRenderer();

	STRATUM_API void create_textures(CommandBuffer& commandBuffer);

	STRATUM_API void new_frame(const Window& window, float deltaTime);
	STRATUM_API void pre_render(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer);
};

} 