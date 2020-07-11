#pragma once

#include <Content/Asset.hpp>
#include <Content/Texture.hpp>
#include <Util/Util.hpp>
#include <Math/Geometry.hpp>
#include <Shaders/include/shadercompat.h>

class Camera;

struct FontGlyph {
	char mCharacter;
	float mAdvance;
	float2 mOffset;
	float2 mExtent;
	fRect2D mTextureRect;

	float mKerning[0xFF];
};

class Font : public Asset {
public:
	const std::string mName;

	ENGINE_EXPORT ~Font() override;

	inline ::Texture* Texture() const { return mTexture; };

	ENGINE_EXPORT const FontGlyph* Glyph(uint32_t c) const;
	ENGINE_EXPORT float Kerning(uint32_t from, uint32_t to) const;

	ENGINE_EXPORT uint32_t GenerateGlyphs(const std::string& str, AABB* aabb, std::vector<TextGlyph>& glyph, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN, TextAnchor verticalAnchor = TEXT_ANCHOR_MIN) const;

	// Character extent in the vertical direction, in pixels
	inline float CharacterHeight() const { return mCharacterHeight; };
	inline float LineSpacing() const { return mLineSpace; };

private:
	friend class AssetManager;
	ENGINE_EXPORT Font(const std::string& name, Device* device, const std::string& filename, float pixelSize);

	float mCharacterHeight;
	float mAscender;
	float mDescender;
	float mLineSpace;

	FontGlyph mGlyphs[0xFF];
	::Texture* mTexture;
};

