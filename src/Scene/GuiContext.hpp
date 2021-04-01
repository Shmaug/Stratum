#pragma once

#include "../Core/CommandBuffer.hpp"
#include "../Core/Window.hpp"

namespace stm {

class GuiContext {
private:
	shared_ptr<DescriptorSet> mDescriptorSet;
	shared_ptr<class Mesh> mMesh;
	shared_ptr<Texture> mFontsTexture;
	shared_ptr<Sampler> mFontsSampler;
	TextureView mFontsTextureView;
	shared_ptr<SpirvModule> mPipeline;

public:
	STRATUM_API GuiContext(CommandBuffer& commandBuffer);
	STRATUM_API void OnDraw(CommandBuffer& commandBuffer, class Camera& camera);
};

}