#pragma once

#include "../Core/CommandBuffer.hpp"
#include "../Core/Window.hpp"
#include "Scene.hpp"

namespace stm {

class GuiElement {
private:
	vector<GuiElement*> mChildren;
public:
	fRect2D mRect;
	fRect2D mClipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f);
	float4 mTint;
	shared_ptr<Texture> mTexture;
	float4 mTextureST = float4(0, 0, 1, 1);

	inline fRect2D Embed(const fRect2D& r, float pad = 0) const { return fRect2D(mRect.mOffset + r.mOffset + pad, r.mSize - 2*pad); }
	inline fRect2D EmbedRelative(const fRect2D& r, float pad = 0) const { return Embed(fRect2D(mRect.mOffset + r.mOffset*mRect.mSize, r.mSize*mRect.mSize), pad); }
	
	inline virtual GuiElement* AddChild(GuiElement* elem) { mChildren.push_back(elem); return elem; }
	inline virtual void RemoveChild(GuiElement* elem) { mChildren.erase(ranges::find(mChildren, elem)); }

	inline virtual void OnInteract(const InputState* state) {}
	inline virtual void OnPreRender(CommandBuffer& commandBuffer, Camera& camera) {

	}
	inline virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) {
		
	}
};

class GuiLabel : public GuiElement {
	string mText;
	TextAnchor mHorizontalAnchor = TextAnchor::eMid;
	TextAnchor mVerticalAnchor = TextAnchor::eMid;
	shared_ptr<Font> mFont;
	float mFontScale = 1;
};
class GuiCheckbox : public GuiElement {
	bool mValue;
};
class GuiSlider : public GuiElement {
	float mValue;
	float mMin;
	float mMax;
};
class GuiRangeSlider : public GuiElement {
	float2 mValues;
	float mMin;
	float mMax;
};

enum GrowAxis { TopDown, BottomUp, LeftRight, RightLeft };
template<GrowAxis axis = GrowAxis::TopDown>
class GuiStack : public GuiElement {
	float mStackOffset;
	
	inline void Space(float size) { mStackOffset += size; }

	inline GuiElement* AddChild(GuiElement* elem) override {
		switch (axis) {
		case GrowAxis::TopDown:
			elem->mRect.mSize.x = mRect.x;
			elem->mRect.mOffset.y = mRect.mSize.y - elem->mRect.mSize.y - mStackOffset;
			mStackOffset += elem->mRect.mSize.y;
			break;
		case GrowAxis::BottomUp:
			elem->mRect.mSize.x = mRect.x;
			elem->mRect.mOffset.y = mStackOffset;
			mStackOffset += elem->mRect.mSize.y;
			break;
		case GrowAxis::LeftRight:
			elem->mRect.mSize.y = mRect.mSize.y;
			elem->mRect.mOffset.x = mStackOffset;
			mStackOffset += elem->mRect.mSize.x;
			break;
		case GrowAxis::RightLeft:
			elem->mRect.mSize.y = mRect.mSize.y;
			elem->mRect.mOffset.x = mRect.mSize.x - elem->mRect.mSize.x - mStackOffset;
			mStackOffset += elem->mRect.mSize.x;
			break;
		}
		return GuiElement::AddChild(elem);
	}
};


class GuiContext {
private:
	stm::Scene& mScene;

	unordered_map<string, shared_ptr<Material>> mMaterials;
	shared_ptr<Texture> mIconsTexture;

	map<string, uint32_t> mHotControl;
	uint32_t mNextControlId;
	
	friend class Scene;
	STRATUM_API GuiContext(Scene& scene);

	STRATUM_API void OnPreRender(CommandBuffer& commandBuffer);
	STRATUM_API void OnDraw(CommandBuffer& commandBuffer, Camera& camera);

public:	
	// Draw a circle facing in the z direction
	STRATUM_API void WireCircle(const float3& center, float radius, const fquat& rotation, const float4& color);
	STRATUM_API void WireSphere(const float3& center, float radius, const fquat& rotation, const float4& color);
	STRATUM_API void WireCube  (const float3& center, const float3& extents, const fquat& rotation, const float4& color);

	STRATUM_API bool PositionHandle(const fquat& plane, float3& position, float radius = .1f, const float4& color = float4(1));
	STRATUM_API bool RotationHandle(const float3& center, fquat& rotation, float radius = .125f, float sensitivity = .3f);
};

}