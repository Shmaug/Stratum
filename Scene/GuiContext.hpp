#pragma once

#include <stack>

#include <Content/Font.hpp>
#include <Core/CommandBuffer.hpp>
#include <Util/Util.hpp>

class AssetManager;
class InputManager;
class Camera;

class GuiContext {
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

		ENGINE_EXPORT fRect2D Get(float size);
	};

	::Device* mDevice;
	::InputManager* mInputManager;

	std::vector<Texture*> mTextureArray;
	std::unordered_map<Texture*, uint32_t> mTextureMap;

	std::vector<GuiString> mScreenStrings;
	std::vector<GuiString> mWorldStrings;

	std::vector<TextGlyph> mGlyphs;
	std::vector<GlyphVertex> mGlyphVertices;
	std::vector<float4x4> mGlyphTransforms;

	std::vector<float3> mLinePoints;
	std::vector<float4x4> mLineTransforms;
	std::vector<GuiLine> mScreenLines;
	std::vector<GuiLine> mWorldLines;

	std::vector<GuiRect> mScreenRects;
	std::vector<GuiRect> mScreenTextureRects;
	std::vector<GuiRect> mWorldRects;
	std::vector<GuiRect> mWorldTextureRects;

	std::unordered_map<std::string, uint32_t> mHotControl;
	std::unordered_map<std::string, uint32_t> mLastHotControl;
	std::unordered_map<uint32_t, std::variant<float, std::string>> mControlData;
	uint32_t mNextControlId;
	
	std::stack<GuiLayout> mLayoutStack;

	Texture* mIconsTexture;

	friend class Scene;
	ENGINE_EXPORT GuiContext(::Device* device, ::InputManager* inputManager);
	ENGINE_EXPORT void Reset();
	ENGINE_EXPORT void PreBeginRenderPass(CommandBuffer* commandBuffer);
	ENGINE_EXPORT void Draw(CommandBuffer* commandBuffer, PassType pass, Camera* camera);

public:
	struct LayoutTheme {
		float4 mBackgroundColor;
		float4 mTextColor;
		float4 mControlBackgroundColor;

		float4 mButtonColor;
		float4 mSliderColor;
		float4 mSliderKnobColor;

		Font* mControlFont;
		Font* mTitleFont;
		float mControlFontHeight;
		float mTitleFontHeight;

		float mControlSize; // Height of a control in a vertical layout
		float mControlPadding; // Padding between controls
		float mSliderBarSize;
		float mSliderKnobSize;
		float mScrollBarThickness;
	};

	static const uint32_t mRenderQueue = 4000;

	LayoutTheme mLayoutTheme;

	inline ::Device* Device() const { return mDevice; }
	inline ::InputManager* InputManager() const { return mInputManager; }

	ENGINE_EXPORT fRect2D BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect);
	ENGINE_EXPORT fRect2D BeginWorldLayout(LayoutAxis axis, const float4x4& transform, const fRect2D& rect);
	
	ENGINE_EXPORT fRect2D BeginSubLayout(LayoutAxis axis, float size);
	ENGINE_EXPORT fRect2D BeginScrollSubLayout(float size, float contentSize);
	ENGINE_EXPORT void EndLayout();
	inline float LayoutDepth() { return mLayoutStack.empty() ? 0 : mLayoutStack.top().mLayoutDepth; };
	inline fRect2D LayoutClipRect() { return mLayoutStack.empty() ? fRect2D(-1e10f, 1e20f) : mLayoutStack.top().mClipRect; };

	ENGINE_EXPORT void LayoutSpace(float size);
	ENGINE_EXPORT void LayoutSeparator();
	ENGINE_EXPORT bool LayoutTextButton(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT bool LayoutImageButton(const float2& size, Texture* texture, const float4& textureST = float4(1, 1, 0, 0));
	ENGINE_EXPORT void LayoutLabel(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT void LayoutTitle(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT bool LayoutToggle(const std::string& label, bool& value);
	ENGINE_EXPORT bool LayoutSlider(const std::string& label, float& value, float minimum, float maximum);
	ENGINE_EXPORT bool LayoutRangeSlider(const std::string& label, float2& values, float minimum, float maximum);


	ENGINE_EXPORT void PolyLine(const float3* points, uint32_t pointCount, const float4& color, float thickness, const float3& offset, const float3& scale, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT void PolyLine(const float4x4& transform, const float3* points, uint32_t pointCount, const float4& color, float thickness, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT bool PositionHandle(const std::string& controlName, const quaternion& plane, float3& position, float radius = .1f, const float4& color = float4(1));
	ENGINE_EXPORT bool RotationHandle(const std::string& controlName, const float3& center, quaternion& rotation, float radius = .125f, float sensitivity = .3f);
		
	// Draw a circle facing in the z direction
	ENGINE_EXPORT void WireCircle(const float3& center, float radius, const quaternion& rotation, const float4& color);
	ENGINE_EXPORT void WireCube(const float3& center, const float3& extents, const quaternion& rotation, const float4& color);
	ENGINE_EXPORT void WireSphere(const float3& center, float radius, const float4& color);

	// Draw a string on the screen. Returns the bounds of the drawn glyphs
	ENGINE_EXPORT void DrawString(const float2& screenPos, float z, Font* font, float pixelHeight, const std::string& str, const float4& color, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a string in the world. Returns the bounds of the drawn glyphs
	ENGINE_EXPORT void DrawString(const float4x4& transform, const float2& offset, Font* font, float pixelHeight, const std::string& str, const float4& color, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a rectangle on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT void Rect(const fRect2D& screenRect, float z, const float4& color, Texture* texture = nullptr, const float4& textureST = float4(1,1,0,0), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a rectangle in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT void Rect(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture = nullptr, const float4& textureST = float4(1, 1, 0, 0), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a label on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT void Label(const fRect2D& screenRect, float z, Font* font, float fontPixelHeight,
		const std::string& text, const float4& textColor, const float4& bgcolor,
		TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a label in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT void Label(const float4x4& transform, const fRect2D& rect, Font* font, float fontPixelHeight,
		const std::string& text, const float4& textColor, const float4& bgcolor,
		TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	
	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT bool TextButton(const fRect2D& screenRect, float z, Font* font, float fontPixelHeight, const std::string& text, const float4& textColor, const float4& color, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT bool TextButton(const float4x4& transform, const fRect2D& rect, Font* font, float fontPixelHeight, const std::string& text, const float4& textColor, const float4& color, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT bool ImageButton(const fRect2D& screenRect, float z, Texture* texture, const float4& color = 1, const float4& textureST = float4(0, 0, 1, 1), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT bool ImageButton(const float4x4& transform, const fRect2D& rect, Texture* texture, const float4& color = 1, const float4& textureST = float4(0, 0, 1, 1), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT bool Slider(const fRect2D& screenRect, float z, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT bool Slider(const float4x4& transform, const fRect2D& rect, float& value, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT bool RangeSlider(const fRect2D& screenRect, float z, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT bool RangeSlider(const float4x4& transform, const fRect2D& rect, float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
};