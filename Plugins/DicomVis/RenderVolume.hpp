#pragma once

#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>

#include "ImageLoader.hpp"

namespace stm {
namespace shader_interop {
#include "Shaders/common.hlsli"
}
}

namespace dcmvs {

using namespace stm;

enum class OrganMaskBits : uint32_t {
	eBladder = 1,
	eKidney = 2,
	eColon = 4,
	eSpleen = 8,
	eIleum = 16,
	eAorta = 32,
	eAll = 0x3F
};
using OrganMask = vk::Flags<OrganMaskBits>;

class RenderVolume : public Object {
	private:
	// The volume loaded directly from the folder
	shared_ptr<Texture> mRawVolume = nullptr;
	// The mask loaded directly from the folder
	shared_ptr<Texture> mRawMask = nullptr;
	// The baked volume. This CAN be nullptr, in which case the pipeline will use the raw volume to compute colors on the fly.
	shared_ptr<Texture> mBakedVolume = nullptr;
	bool mBakeDirty = false;
	// The gradient of the volume. This CAN be nullptr, in which case the pipeline will compute the gradient on the fly.
	shared_ptr<Texture> mGradient = nullptr;
	bool mGradientDirty = false;
	
	shared_ptr<Buffer> mUniformBuffer;
	shared_ptr<Pipeline> mPrecomputePipeline;
	shared_ptr<Pipeline> mRenderPipeline;

public:
	enum class ShadingMode {
		eNone,
		eLocal
	};

	float mSampleRate = 1.f;
	bool mColorize = false;
	float mDensityScale = 1.f;
	Vector2f mHueRange = Vector2f(.01f, .5f);
	Vector2f mRemapRange = Vector2f(.125f, 1.f);
	ShadingMode mShadingMode = {};
	OrganMask mOrganMask = {};

	PLUGIN_EXPORT RenderVolume(const string& name, Scene* scene, Device& device, const fs::path& imageStackFolder);

	PLUGIN_EXPORT void BakeRender(CommandBuffer& commandBuffer);

	PLUGIN_EXPORT void DrawGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui);
	PLUGIN_EXPORT void Draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, Camera& camera);
};

}