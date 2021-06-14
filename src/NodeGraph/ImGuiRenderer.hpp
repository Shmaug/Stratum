#pragma once

#include <imgui.h>

#include "../Core/Material.hpp"
#include "NodeGraph.hpp"

namespace stm {

class ImGuiRenderer {
private:
	shared_ptr<Material> mMaterial;
	Geometry mGeometry;
	Buffer::StrideView mIndices;
	const ImDrawData* mDrawData;
	
public:
	STRATUM_API ImGuiRenderer(NodeGraph& nodeGraph, const shared_ptr<SpirvModule>& vs, const shared_ptr<SpirvModule>& fs);

	STRATUM_API void create_textures(CommandBuffer& commandBuffer);

	STRATUM_API void new_frame(const Window& window, float deltaTime);
	STRATUM_API void pre_render(CommandBuffer& commandBuffer);
	STRATUM_API void draw(CommandBuffer& commandBuffer);
};

} 