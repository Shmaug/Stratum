#pragma once

#include <Core/Device.hpp>

class Font {
public:
	const std::string mName;

	STRATUM_API ~Font();

	inline float Ascent(float pixelHeight) const { return mAscent*pixelHeight/mEmSize; }
	inline float Descent(float pixelHeight) const { return mDescent*pixelHeight/mEmSize; }
	inline float LineSpace(float pixelHeight) const { return mLineGap*pixelHeight/mEmSize; }
	STRATUM_API void GenerateGlyphs(std::vector<GlyphRect>& glyphs, AABB& bounds, const std::string& str, float pixelHeight, const float2& offset = 0, TextAnchor horizontalAnchor = TextAnchor::eMin) const;
	inline Texture* SDF() const { return mSDF; }

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
	STRATUM_API Font(const std::string& name, Device* device, const std::string& filename);

	float mEmSize;
	
	float mAscent;
	float mDescent;
	float mLineGap;
	float mSpaceAdvance;
	float mTabAdvance;

	Texture* mSDF;

	std::unordered_map<uint32_t, Glyph> mGlyphs;
};

