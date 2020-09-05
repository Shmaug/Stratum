#pragma once

#include <Data/Asset.hpp>

class Font : public Asset {
public:
	inline float Ascent(float pixelHeight) const { return mAscent*pixelHeight/mEmSize; }
	inline float Descent(float pixelHeight) const { return mDescent*pixelHeight/mEmSize; }
	inline float LineSpace(float pixelHeight) const { return mLineGap*pixelHeight/mEmSize; }
	STRATUM_API void GenerateGlyphs(std::vector<GlyphRect>& glyphs, AABB& bounds, const std::string& str, float pixelHeight, const float2& offset = 0, TextAnchor horizontalAnchor = TextAnchor::eMin) const;
	inline stm_ptr<Texture> SDF() const { return mSDF; }

private:
	struct Glyph {
		float mAdvance;
		float2 mExtent;
		float2 mOffset;
		uint2 mTextureOffset;
		uint2 mTextureExtent;
		std::unordered_map<uint32_t, float> mKerning;
	};
	friend class AssetManager;
	STRATUM_API Font(const fs::path& filename, ::Device* device, const std::string& name);

	float mEmSize;
	
	float mAscent;
	float mDescent;
	float mLineGap;
	float mSpaceAdvance;
	float mTabAdvance;

	stm_ptr<Texture> mSDF;

	std::unordered_map<uint32_t, Glyph> mGlyphs;
};

