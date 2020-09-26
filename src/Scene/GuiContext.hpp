#pragma once

#include <Data/Font.hpp>
#include <Data/Pipeline.hpp>
#include <Core/CommandBuffer.hpp>

namespace stm {

class GuiContext {
public:
	enum class LayoutAxis : uint32_t {
		eHorizontal = 0,
		eVertical = 1
	};
	struct LayoutTheme {
		float4 mBackgroundColor;
		float4 mTextColor;
		float4 mControlBackgroundColor;

		float4 mButtonColor;
		float4 mSliderColor;
		float4 mSliderKnobColor;

		std::shared_ptr<Font> mControlFont;
		std::shared_ptr<Font> mTitleFont;
		float mControlFontHeight;
		float mTitleFontHeight;

		float mControlSize; // Height of a control in a vertical layout
		float mControlPadding; // Padding between controls
		float mSliderBarSize;
		float mSliderKnobSize;
		float mScrollBarThickness;
	};

private:
	#pragma pack(push)
	#pragma pack(1)
	struct GuiRect {
		float4x4 mTransform;
		float4 mScaleTranslate;
		fRect2D mClipBounds;
		float4 mColor;

		float4 mTextureST;
		uint32_t mTextureIndex;
		float mDepth;
		uint32_t pad[2];
	};
	#pragma pack(pop)
	
	struct GuiLine {
		fRect2D mClipBounds;
		float4 mColor;
		float mThickness;
		uint32_t mTransformIndex;
		uint32_t mIndex;
		uint32_t mCount;
	};
	struct GuiString {
		uint32_t mGlyphIndex;
		uint32_t mGlyphCount;
		float4 mColor;
		fRect2D mClipBounds;
		uint32_t mSdfIndex;
		uint32_t mTransformIndex;
		float mDepth;
	};
	struct GuiLayout {
		float4x4 mTransform;
		bool mScreenSpace;

		LayoutAxis mAxis;
		fRect2D mRect;
		fRect2D mClipRect;
		float mLayoutPosition;
		float mLayoutDepth;

		STRATUM_API fRect2D Get(float size);
	};

	std::vector<std::shared_ptr<Texture>> mTextureArray;
	std::unordered_map<std::shared_ptr<Texture>, uint32_t> mTextureMap;

	std::vector<GuiString> mScreenStrings;
	std::vector<GuiString> mWorldStrings;

	std::vector<GlyphRect> mStringGlyphs;
	std::vector<std::shared_ptr<Texture>> mUniqueStringSDFs;
	std::vector<float4x4> mStringTransforms;

	std::vector<float3> mLinePoints;
	std::vector<float4x4> mLineTransforms;
	std::vector<GuiLine> mScreenLines;
	std::vector<GuiLine> mWorldLines;

	std::vector<GuiRect> mScreenRects;
	std::vector<GuiRect> mScreenTextureRects;
	std::vector<GuiRect> mWorldRects;
	std::vector<GuiRect> mWorldTextureRects;

	std::map<std::string, uint32_t> mHotControl;
	std::map<std::string, uint32_t> mLastHotControl;
	std::unordered_map<uint32_t, std::variant<float, std::string>> mControlData;
	uint32_t mNextControlId;
	
	std::stack<GuiLayout> mLayoutStack;
	std::stack<LayoutTheme> mLayoutThemeStack;

	std::shared_ptr<Texture> mIconsTexture;

	friend class Scene;
	STRATUM_API GuiContext(Device* device, InputManager* inputManager);
	STRATUM_API void Reset();
	STRATUM_API void OnPreRender(CommandBuffer& commandBuffer);
	STRATUM_API void OnDraw(CommandBuffer& commandBuffer, Camera& camera, std::shared_ptr<DescriptorSet> perCamera);

public:
	stm::Device* const mDevice;
	stm::InputManager* const mInputManager;
	LayoutTheme mLayoutTheme;

	inline void PushLayoutTheme() { mLayoutThemeStack.push(mLayoutTheme); }
	inline void PopLayoutTheme() { mLayoutTheme = mLayoutThemeStack.top(); mLayoutThemeStack.pop(); }

	STRATUM_API fRect2D BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect);
	STRATUM_API fRect2D BeginWorldLayout(LayoutAxis axis, const float4x4& transform, const fRect2D& rect);
	
	STRATUM_API fRect2D BeginSubLayout(LayoutAxis axis, float size);
	STRATUM_API fRect2D BeginScrollSubLayout(float size, float contentSize);
	STRATUM_API void EndLayout();
	inline float LayoutDepth() { return mLayoutStack.empty() ? 0 : mLayoutStack.top().mLayoutDepth; };
	inline fRect2D LayoutClipRect() { return mLayoutStack.empty() ? fRect2D(-1e10f, 1e20f) : mLayoutStack.top().mClipRect; };

	STRATUM_API void LayoutSpace(float size);
	STRATUM_API void LayoutSeparator();
	STRATUM_API bool LayoutTextButton(const std::string& text, TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid);
	STRATUM_API bool LayoutImageButton(const float2& size, std::shared_ptr<Texture> texture, const float4& textureST = float4(1, 1, 0, 0));
	STRATUM_API void LayoutLabel(const std::string& text, TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid);
	STRATUM_API void LayoutTitle(const std::string& text, TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid);
	STRATUM_API bool LayoutToggle(const std::string& label, bool& value);
	STRATUM_API bool LayoutSlider(const std::string& label, float& value, float minimum, float maximum);
	STRATUM_API bool LayoutRangeSlider(const std::string& label, float2& values, float minimum, float maximum);


	STRATUM_API void PolyLine(const float3* points, uint32_t pointCount, const float4& color, float thickness, const float3& offset, const float3& scale, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	STRATUM_API void PolyLine(const float4x4& transform, const float3* points, uint32_t pointCount, const float4& color, float thickness, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	STRATUM_API bool PositionHandle(const std::string& controlName, const quaternion& plane, float3& position, float radius = .1f, const float4& color = float4(1));
	STRATUM_API bool RotationHandle(const std::string& controlName, const float3& center, quaternion& rotation, float radius = .125f, float sensitivity = .3f);
		
	// Draw a circle facing in the z direction
	STRATUM_API void WireCircle(const float3& center, float radius, const quaternion& rotation, const float4& color);
	STRATUM_API void WireCube(const float3& center, const float3& extents, const quaternion& rotation, const float4& color);
	STRATUM_API void WireSphere(const float3& center, float radius, const float4& color);

	// Draw a string on the screen. Returns the bounds of the drawn glyphs
	STRATUM_API void DrawString(const float2& screenPos, float z, std::shared_ptr<Font> font, float pixelHeight, const std::string& str, const float4& color, TextAnchor horizontalAnchor = TextAnchor::eMin, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a string in the world. Returns the bounds of the drawn glyphs
	STRATUM_API void DrawString(const float4x4& transform, const float2& offset, std::shared_ptr<Font> font, float pixelHeight, const std::string& str, const float4& color, TextAnchor horizontalAnchor = TextAnchor::eMin, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a rectangle on the screen, "size" pixels big with the bottom-left corner at screenPos
	STRATUM_API void Rect(const fRect2D& screenRect, float z, const float4& color, std::shared_ptr<Texture> texture = nullptr, const float4& textureST = float4(1,1,0,0), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a rectangle in the world, "size" units big with the bottom-left corner at screenPos
	STRATUM_API void Rect(const float4x4& transform, const fRect2D& rect, const float4& color, std::shared_ptr<Texture> texture = nullptr, const float4& textureST = float4(1, 1, 0, 0), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a label on the screen, "size" pixels big with the bottom-left corner at screenPos
	STRATUM_API void Label(const fRect2D& screenRect, float z, std::shared_ptr<Font> font, float fontPixelHeight,
		const std::string& text, const float4& textColor, const float4& bgcolor,
		TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a label in the world, "size" units big with the bottom-left corner at screenPos
	STRATUM_API void Label(const float4x4& transform, const fRect2D& rect, std::shared_ptr<Font> font, float fontPixelHeight,
		const std::string& text, const float4& textColor, const float4& bgcolor,
		TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	
	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	STRATUM_API bool TextButton(const fRect2D& screenRect, float z, std::shared_ptr<Font> font, float fontPixelHeight, const std::string& text, const float4& textColor, const float4& color, TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	STRATUM_API bool TextButton(const float4x4& transform, const fRect2D& rect, std::shared_ptr<Font> font, float fontPixelHeight, const std::string& text, const float4& textColor, const float4& color, TextAnchor horizontalAnchor = TextAnchor::eMid, TextAnchor verticalAnchor = TextAnchor::eMid, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	STRATUM_API bool ImageButton(const fRect2D& screenRect, float z, std::shared_ptr<Texture> texture, const float4& color = 1, const float4& textureST = float4(0, 0, 1, 1), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	STRATUM_API bool ImageButton(const float4x4& transform, const fRect2D& rect, std::shared_ptr<Texture> texture, const float4& color = 1, const float4& textureST = float4(0, 0, 1, 1), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	STRATUM_API bool Slider(const fRect2D& screenRect, float z, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	STRATUM_API bool Slider(const float4x4& transform, const fRect2D& rect, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	STRATUM_API bool RangeSlider(const fRect2D& screenRect, float z, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	STRATUM_API bool RangeSlider(const float4x4& transform, const fRect2D& rect, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
};

}