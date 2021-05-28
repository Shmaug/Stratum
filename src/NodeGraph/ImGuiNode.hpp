#pragma once

#include <imgui/imgui.h>

#include "../Core/Material.hpp"
#include "../Core/Texture.hpp"

namespace stm {

class ImGuiNode {
private:
	//Texture::View mFonts;
	shared_ptr<Material> mMaterial;
	GeometryData mGeometry;
	Buffer::StrideView mIndexBuffer;
public:
	STRATUM_API ImGuiNode(const string& name, CommandBuffer& commandBuffer, shared_ptr<SpirvModule> vs_texture, shared_ptr<SpirvModule> fs_texture);
	STRATUM_API void NewFrame(const Window& window);
	STRATUM_API void PreRender(CommandBuffer& commandBuffer);
	STRATUM_API void Render(CommandBuffer& commandBuffer);
};

} 