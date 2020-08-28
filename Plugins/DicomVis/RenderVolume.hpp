#pragma once

#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>

#include "ImageLoader.hpp"
#include "Shaders/common.hlsli"

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
	Texture* mRawVolume = nullptr;
	// The mask loaded directly from the folder
	Texture* mRawMask = nullptr;
	// The baked volume. This CAN be nullptr, in which case the pipeline will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume = nullptr;
	bool mBakeDirty = false;
	// The gradient of the volume. This CAN be nullptr, in which case the pipeline will compute the gradient on the fly.
	Texture* mGradient = nullptr;
	bool mGradientDirty = false;
	
	Device* mDevice;
	Buffer* mUniformBuffer = nullptr;

public:
	enum class ShadingMode {
		eNone,
		eLocal
	};

	float mSampleRate = 1.f;
	bool mColorize = false;
	float mDensityScale = 1.f;
	float2 mHueRange = float2(.01f, .5f);
	float2 mRemapRange = float2(.125f, 1.f);
	ShadingMode mShadingMode = {};
	OrganMask mOrganMask = {};

	RenderVolume(const std::string& name, Device* device, const fs::path& imageStackFolder);
  ~RenderVolume();
	void DrawGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui);
	void UpdateBake(CommandBuffer* commandBuffer);
	void Draw(CommandBuffer* commandBuffer, Framebuffer* framebuffer, Camera* camera);
};
