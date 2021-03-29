#pragma once

#include "../Core/CommandBuffer.hpp"
#include "../Core/Window.hpp"

namespace stm {

class GuiContext {
private:
	unordered_map<string, shared_ptr<class Material>> mMaterials;
	shared_ptr<Texture> mFontsTexture;
	shared_ptr<Sampler> mFontsSampler;
	TextureView mFontsTextureView;

public:
	STRATUM_API GuiContext();
	STRATUM_API void OnDraw(CommandBuffer& commandBuffer, class Camera& camera);
};

}