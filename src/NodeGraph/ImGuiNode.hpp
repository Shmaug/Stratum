#pragma once

#include <imgui/imgui.h>

#include "../Core/Material.hpp"
#include "../Core/Texture.hpp"
#include "RenderNode.hpp"

namespace stm {

class ImGuiNode {
private:
	//Texture::View mFonts;
	shared_ptr<Material> mMaterial;
	Geometry mGeometry;
	Buffer::StrideView mIndexBuffer;
	Buffer::StrideView mVertexBuffer;
	RenderNode* mRenderNode;
public:
	STRATUM_API ImGuiNode(const string& name, NodeGraph& nodeGraph, Device& device, shared_ptr<SpirvModule> vsTexture, shared_ptr<SpirvModule> fsTexture);
	
	STRATUM_API void LoadFonts(CommandBuffer& commandBuffer);
	STRATUM_API void NewFrame(const Window& window);
	STRATUM_API void PreRender(CommandBuffer& commandBuffer);
	STRATUM_API void Render(CommandBuffer& commandBuffer);
};

} 