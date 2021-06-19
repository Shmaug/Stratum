#pragma once

#include <NodeGraph/RenderGraph.hpp>
#include "ImageLoader.hpp"

#include <imgui.h>

namespace stm {
namespace shader_interop {
#include "Shaders/include/transform.hlsli"
}
}

namespace dcmvs {

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

class VolumeRenderer {
private:
	NodeGraph::Node& mNode;

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
	
	shared_ptr<Material> mBakeColorMaterial;
	shared_ptr<Material> mBakeGradientMaterial;
	shared_ptr<Material> mRenderMaterial;

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

	PLUGIN_EXPORT VolumeRenderer(NodeGraph::Node& node, const string& name, Device& device, const fs::path& imageStackFolder);

	PLUGIN_EXPORT void imgui();

	PLUGIN_EXPORT void bake(CommandBuffer& commandBuffer);

	PLUGIN_EXPORT void draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer);
};

}