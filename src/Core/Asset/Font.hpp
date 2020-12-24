#pragma once

#include "Asset.hpp"

namespace stm {

enum class TextAnchor { eMin, eMid, eMax };

class Font : public Asset {
public:
	inline float Ascent(float pixelHeight) const { return mAscent*pixelHeight/mEmSize; }
	inline float Descent(float pixelHeight) const { return mDescent*pixelHeight/mEmSize; }
	inline float LineSpace(float pixelHeight) const { return mLineSpace*pixelHeight/mEmSize; }
	inline shared_ptr<Texture> SDF() const { return mSDF; }

	STRATUM_API void GenerateGlyphs(vector<GlyphRect>& glyphs, fAABB& bounds, const string& str, float pixelHeight, const float2& offset = 0, TextAnchor horizontalAnchor = TextAnchor::eMin) const;

private:
	struct Glyph {
		float mAdvance;
		float2 mExtent;
		float2 mOffset;
		uint2 mTextureOffset;
		uint2 mTextureExtent;
		unordered_map<uint32_t, float> mKerning;
	};

	friend class Device;
	STRATUM_API Font(stm::Device& device, const fs::path& filename);

	shared_ptr<Texture> mSDF;
	unordered_map<uint32_t, Glyph> mGlyphs;
	string mName;
	float mEmSize;
	float mAscent;
	float mDescent;
	float mLineSpace;
	float mSpaceAdvance;
	float mTabAdvance;
};

}