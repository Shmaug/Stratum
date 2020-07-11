#pragma once

#include <stack>

#include <Content/Font.hpp>
#include <Core/CommandBuffer.hpp>
#include <Util/Util.hpp>

class AssetManager;
class InputManager;
class Camera;

// Immediate mode GUI
// Calls are batched and rendered all at once during Scene::Render(), at GUI::mRenderQueue
class GUI {
private:
	#pragma pack(push)
	#pragma pack(1)
	struct GuiRect {
		float4x4 ObjectToWorld;
		float4 ScaleTranslate;
		fRect2D Bounds;
		float4 Color;

		float4 TextureST;
		uint32_t TextureIndex;
		float Depth;
		uint32_t pad[2];
	};
	#pragma pack(pop)
	
	struct GuiLine {
		fRect2D mBounds;
		float4 mColor;
		float mThickness;
		uint32_t mTransformIndex;
		uint32_t mIndex;
		uint32_t mCount;
	};
	struct GuiString {
		float4x4 mTransform;
		float4 mColor;
		fRect2D mBounds;
		float2 mOffset;
		Font* mFont;
		float mScale;
		float mDepth;
		TextAnchor mHorizontalAnchor;
		TextAnchor mVerticalAnchor;
		std::string mString;
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

	static std::vector<Texture*> mTextureArray;
	static std::unordered_map<Texture*, uint32_t> mTextureMap;

	static std::vector<GuiString> mScreenStrings;
	static std::vector<GuiString> mWorldStrings;

	static std::vector<float3> mLinePoints;
	static std::vector<float4x4> mLineTransforms;
	static std::vector<GuiLine> mScreenLines;
	static std::vector<GuiLine> mWorldLines;

	static std::vector<GuiRect> mScreenRects;
	static std::vector<GuiRect> mScreenTextureRects;
	static std::vector<GuiRect> mWorldRects;
	static std::vector<GuiRect> mWorldTextureRects;

	static std::unordered_map<std::string, uint32_t> mHotControl;
	static std::unordered_map<std::string, uint32_t> mLastHotControl;
	static std::unordered_map<uint32_t, std::variant<float, std::string>> mControlData;
	static uint32_t mNextControlId;
	
	static InputManager* mInputManager;

	static std::stack<GuiLayout> mLayoutStack;

	static Texture* mIconsTexture;

	friend class Stratum;
	friend class Scene;
	ENGINE_EXPORT static void Initialize(Device* device, InputManager* inputManager);
	ENGINE_EXPORT static void Reset(CommandBuffer* commandBuffer);
	ENGINE_EXPORT static void PreBeginRenderPass(CommandBuffer* commandBuffer);
	ENGINE_EXPORT static void Draw(CommandBuffer* commandBuffer, PassType pass, Camera* camera);

public:
	static const uint32_t mRenderQueue = 4000;

	struct LayoutTheme {
		float4 mBackgroundColor;
		float4 mTextColor;
		float4 mControlBackgroundColor;

		float4 mButtonColor;
		float4 mSliderColor;
		float4 mSliderKnobColor;

		Font* mControlFont;
		Font* mTitleFont;
		float mControlFontSize;
		float mTitleFontSize;

		float mControlSize; // Height of a control in a vertical layout
		float mControlPadding; // Padding between controls
		float mSliderBarSize;
		float mSliderKnobSize;
		float mScrollBarThickness;
	};
	ENGINE_EXPORT static LayoutTheme mLayoutTheme;

	ENGINE_EXPORT static fRect2D BeginScreenLayout(LayoutAxis axis, const fRect2D& screenRect);
	ENGINE_EXPORT static fRect2D BeginWorldLayout(LayoutAxis axis, const float4x4& transform, const fRect2D& rect);
	
	ENGINE_EXPORT static fRect2D BeginSubLayout(LayoutAxis axis, float size);
	ENGINE_EXPORT static fRect2D BeginScrollSubLayout(float size, float contentSize);
	ENGINE_EXPORT static void EndLayout();
	inline static float LayoutDepth() { return mLayoutStack.empty() ? 0 : mLayoutStack.top().mLayoutDepth; };
	inline static fRect2D LayoutClipRect() { return mLayoutStack.empty() ? fRect2D(-1e10f, 1e20f) : mLayoutStack.top().mClipRect; };

	ENGINE_EXPORT static void LayoutSpace(float size);
	ENGINE_EXPORT static void LayoutSeparator();
	ENGINE_EXPORT static bool LayoutTextButton(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT static bool LayoutImageButton(const float2 size, Texture* texture, const float4& textureST = float4(1, 1, 0, 0));
	ENGINE_EXPORT static void LayoutLabel(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT static void LayoutTitle(const std::string& text, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID);
	ENGINE_EXPORT static bool LayoutToggle(const std::string& label, bool& value);
	ENGINE_EXPORT static bool LayoutSlider(const std::string& label, float& value, float minimum, float maximum);
	ENGINE_EXPORT static bool LayoutRangeSlider(const std::string& label, float2& values, float minimum, float maximum);


	ENGINE_EXPORT static void PolyLine(const float3* points, uint32_t pointCount, const float4& color, float thickness, const float3& offset, const float3& scale, float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT static void PolyLine(const float4x4& transform, const float3* points, uint32_t pointCount, const float4& color, float thickness, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT static bool PositionHandle(const std::string& controlName, const quaternion& plane, float3& position, float radius = .1f, const float4& color = float4(1));
	ENGINE_EXPORT static bool RotationHandle(const std::string& controlName, const float3& center, quaternion& rotation, float radius = .125f, float sensitivity = .3f);
		
	// Draw a circle facing in the z direction
	ENGINE_EXPORT static void WireCircle(const float3& center, float radius, const quaternion& rotation, const float4& color);
	ENGINE_EXPORT static void WireCube(const float3& center, const float3& extents, const quaternion& rotation, const float4& color);
	ENGINE_EXPORT static void WireSphere(const float3& center, float radius, const float4& color);


	// Draw a string on the screen, where screenPos is in pixels and (0,0) is the bottom-left of the screen
	ENGINE_EXPORT static void DrawString(Font* font, const std::string& str, const float4& color, const float2& screenPos, float scale, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN, TextAnchor verticalAnchor = TEXT_ANCHOR_MIN, float z = .0001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a string in the world
	ENGINE_EXPORT static void DrawString(Font* font, const std::string& str, const float4& color, const float4x4& objectToWorld, const float2& offset, float scale, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN, TextAnchor verticalAnchor = TEXT_ANCHOR_MIN, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a rectangle on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT static void Rect(const fRect2D& screenRect, const float4& color, Texture* texture = nullptr, const float4& textureST = float4(1,1,0,0), float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a rectangle in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT static void Rect(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture = nullptr, const float4& textureST = float4(1, 1, 0, 0), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a label on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT static void Label(Font* font, const std::string& text, float textScale,
		const fRect2D& screenRect, const float4& color, const float4& textColor,
		TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a label in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT static void Label(Font* font, const std::string& text, float textScale,
		const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor,
		TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	
	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT static bool TextButton(Font* font, const std::string& text, float textScale, const fRect2D& screenRect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT static bool TextButton(Font* font, const std::string& text, float textScale, const float4x4& transform, const fRect2D& rect, const float4& color, const float4& textColor, TextAnchor horizontalAnchor = TEXT_ANCHOR_MID, TextAnchor verticalAnchor = TEXT_ANCHOR_MID, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	// Draw a button on the screen, "size" pixels big with the bottom-left corner at screenPos
	ENGINE_EXPORT static bool ImageButton(const fRect2D& screenRect, const float4& color, Texture* texture, const float4& textureST = float4(0, 0, 1, 1), float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	// Draw a button in the world, "size" units big with the bottom-left corner at screenPos
	ENGINE_EXPORT static bool ImageButton(const float4x4& transform, const fRect2D& rect, const float4& color, Texture* texture, const float4& textureST = float4(0, 0, 1, 1), const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT static bool Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect,
		const float4& barColor, const float4& knobColor, float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT static bool Slider(float& value, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));

	ENGINE_EXPORT static bool RangeSlider(float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize, const fRect2D& screenRect,
		const float4& barColor, const float4& knobColor, float z = .001f, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
	ENGINE_EXPORT static bool RangeSlider(float2& values, float minimum, float maximum, LayoutAxis axis, float knobSize, const float4x4& transform, const fRect2D& rect,
		const float4& barColor, const float4& knobColor, const fRect2D& clipRect = fRect2D(-1e10f, -1e10f, 1e20f, 1e20f));
};