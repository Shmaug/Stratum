#pragma once

#include <imgui/imgui.h>

#include "Material.hpp"

namespace stm {

class ImGuiNode : public Scene::Node {
private:
	TextureView mFonts;
	shared_ptr<Material> mMaterial;
public:
	STRATUM_API ImGuiNode(Scene& scene, const string& name, CommandBuffer& commandBuffer, shared_ptr<SpirvModule> fs_texture);
	STRATUM_API void Render(CommandBuffer& commandBuffer);
};

}