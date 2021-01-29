#pragma once

#include "../Core/CommandBuffer.hpp"
#include "../Core/Window.hpp"
#include "Scene.hpp"

namespace stm {

class GuiElement : public SceneNode::Component {
private:
	vector<GuiElement*> mChildren;
public:
	AlignedBox2f mRect;
	AlignedBox2f mClipRect = AlignedBox2f(Vector2f::Constant(-1e10f), Vector2f::Constant(1e20f));
	Vector4f mColor;
	shared_ptr<Texture> mTexture;
	Vector4f mTextureST = Vector4f(0, 0, 1, 1);

	inline AlignedBox2f EmbedRelative(const Vector2f& scale const Vector2f& offset) const {
		return AlignedBox2f(mRect.min() * mRect.sizes() + offset*min(), r.mSize*mRect.mSize);
	}

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
	Vector2f mValues;
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
			elem->mRect.mSize.x() = mRect.mSize.x();
			elem->mRect.mOffset.y() = mRect.mSize.y() - elem->mRect.mSize.y() - mStackOffset;
			mStackOffset += elem->mRect.mSize.y();
			break;
		case GrowAxis::BottomUp:
			elem->mRect.mSize.x() = mRect.mSize.x();
			elem->mRect.mOffset.y() = mStackOffset;
			mStackOffset += elem->mRect.mSize.y();
			break;
		case GrowAxis::LeftRight:
			elem->mRect.mSize.y() = mRect.mSize.y();
			elem->mRect.mOffset.x() = mStackOffset;
			mStackOffset += elem->mRect.mSize.x();
			break;
		case GrowAxis::RightLeft:
			elem->mRect.mSize.y() = mRect.mSize.y();
			elem->mRect.mOffset.x() = mRect.mSize.x() - elem->mRect.mSize.x() - mStackOffset;
			mStackOffset += elem->mRect.mSize.x();
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