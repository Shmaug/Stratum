#pragma once

#include "../Core/CommandBuffer.hpp"
#include "../Core/Window.hpp"
#include "Scene.hpp"

namespace stm {

class GuiContext {
private:
	stm::Scene& mScene;

	unordered_map<string, shared_ptr<Material>> mMaterials;
	shared_ptr<Texture> mIconsTexture;
	shared_ptr<Texture> mFontsTexture;
	shared_ptr<Sampler> mFontsSampler;
	TextureView mFontsTextureView;

	unordered_map<string, uint32_t> mHotControl;
	uint32_t mNextControlId;
	
	friend class stm::Scene;
	STRATUM_API GuiContext(stm::Scene& scene);

	STRATUM_API void OnDraw(CommandBuffer& commandBuffer, Camera& camera);

public:
	inline stm::Scene& Scene() const { return mScene; }

	// Draw a circle facing in the z direction
	STRATUM_API void WireCircle(const Vector3f& center, float radius, const fquat& rotation, const Vector4f& color);
	STRATUM_API void WireSphere(const Vector3f& center, float radius, const fquat& rotation, const Vector4f& color);
	STRATUM_API void WireCube  (const Vector3f& center, const Vector3f& extents, const fquat& rotation, const Vector4f& color);

	STRATUM_API bool PositionHandle(const fquat& plane, Vector3f& position, float radius = .1f, const Vector4f& color = Vector4f(1));
	STRATUM_API bool RotationHandle(const Vector3f& center, fquat& rotation, float radius = .125f, float sensitivity = .3f);
};

}