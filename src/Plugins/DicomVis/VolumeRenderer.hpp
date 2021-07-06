#pragma once

#include "../../NodeGraph/RenderGraph.hpp"
#include "../../NodeGraph/ImGuiInstance.hpp"

namespace stm {
namespace hlsl {
#include "../../Shaders/include/transform.hlsli"
}
}

namespace dcmvs {

using namespace stm;
using namespace stm::hlsl;

class VolumeRenderer {
public:
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

	enum class ShadingMode { eNone, eLocal };
	
	NodeGraph::Node& mNode;
	
	Vector3f mVoxelSize;
	float mSampleRate = 1.f;
	bool mColorize = false;
	float mDensityScale = 1.f;
	Vector2f mHueRange = Vector2f(.01f, .5f);
	Vector2f mRemapRange = Vector2f(.125f, 1.f);
	ShadingMode mShadingMode = {};
	OrganMask mOrganMask = {};

	PLUGIN_API VolumeRenderer(NodeGraph::Node& node, const Texture::View& volume, const Texture::View& mask, const Vector3f& voxelSize);
	
	PLUGIN_API void imgui();
	PLUGIN_API void bake(CommandBuffer& commandBuffer);
	PLUGIN_API void draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer);

private:

	// The volume loaded directly from the folder
	Texture::View mRawVolume;
	// The mask loaded directly from the folder
	Texture::View mRawMask;
	// The baked volume. If empty, the pipeline will use the raw volume to compute colors on the fly.
	Texture::View mBakedVolume;
	bool mBakeDirty = false;
	// The gradient of the volume. If empty, the pipeline will compute the gradient on the fly.
	Texture::View mGradient;
	bool mGradientDirty = false;
	
	shared_ptr<Material> mBakeColorMaterial;
	shared_ptr<Material> mBakeGradientMaterial;
	shared_ptr<Material> mRenderMaterial;
};

}