#pragma once

#include <Content/Asset.hpp>
#include <Content/Texture.hpp>
#include <Util/Util.hpp>
#include <Math/Geometry.hpp>
#include <Shaders/include/shadercompat.h>

class Camera;

struct FontGlyph {
	uint32_t mCodepoint;
	int2 mOffset;
	int2 mExtent;
	int32_t mAdvance;
	std::vector<GlyphVertex> mShape;
	std::unordered_map<uint32_t, int32_t> mKerning;
};

class Font : public Asset {
public:
	const std::string mName;

	ENGINE_EXPORT ~Font() override;

	ENGINE_EXPORT void GenerateGlyphs(const std::string& str, float pixelHeight, std::vector<TextGlyph>& glyphs, std::vector<GlyphVertex>& vertices, const float2& offset = 0, AABB* aabb = nullptr, TextAnchor horizontalAnchor = TEXT_ANCHOR_MIN) const;

private:
	friend class AssetManager;
	ENGINE_EXPORT Font(const std::string& name, Device* device, const std::string& filename);

	inline const FontGlyph* Glyph(uint32_t c) const { return mGlyphs.count(c) ? &mGlyphs.at(c) : nullptr; }

	int32_t mAscent;
	int32_t mDescent;
	int32_t mLineSpace;
	int32_t mFontHeight;

	std::unordered_map<uint32_t, FontGlyph> mGlyphs;
};

