#pragma once

#include "Texture.hpp"

namespace stm {

enum class TextAnchor { eMin, eMid, eMax };

class Font {
private:
	struct Glyph {
		float mAdvance;
		AlignedBox2f mBounds;
		AlignedBox2i mTextureBounds;
		unordered_map<uint32_t, float> mKerning;
	};

	shared_ptr<Texture> mSDF;
	unordered_map<uint32_t, Glyph> mGlyphs;
	string mName;
	float mEmSize;
	float mAscent;
	float mDescent;
	float mLineSpace;
	float mSpaceAdvance;
	float mTabAdvance;

public:
	STRATUM_API Font(Device& device, const fs::path& filename);

	inline float Ascent(float pixelHeight) const { return mAscent*pixelHeight/mEmSize; }
	inline float Descent(float pixelHeight) const { return mDescent*pixelHeight/mEmSize; }
	inline float LineSpace(float pixelHeight) const { return mLineSpace*pixelHeight/mEmSize; }
	inline shared_ptr<Texture> SDF() const { return mSDF; }

	template<ranges::range R>
	inline void GenerateGlyphs(R& result, const string& str, float pixelHeight, const Vector2f& offset = Vector2f::Zero(), TextAnchor horizontalAnchor = TextAnchor::eMin) const {
		if (str.empty()) return;

		const Glyph* prev = nullptr;

		auto scale = UniformScaling(pixelHeight / mEmSize);

		Vector2f cur = offset;
		size_t lineStart = result.size();

		for (uint32_t i = 0; i < str.length(); i++) {
			uint32_t codepoint = (uint32_t)str[i];
			
			if (codepoint == '\n') {
				if (horizontalAnchor != TextAnchor::eMin) {
					float o = offset.x() - cur.x();
					if (horizontalAnchor == TextAnchor::eMid) o /= 2;
					ranges::for_each(result | views::drop(lineStart), [](auto& g){ g.translate(Vector2f(o,0)); });
				}
				cur.x() = offset.x();
				cur.y() -= mLineSpace*scale.factor();
				prev = nullptr;
				lineStart = result.size();
				continue;
			}
			else if (codepoint == '\t') {
				prev = nullptr;
				cur.x() = ceilf(cur.x()/(mTabAdvance*scale.factor()))*mTabAdvance*scale.factor();
				continue;
			}
			else if (codepoint == ' ' || mGlyphs.count(codepoint) == 0) {
				prev = nullptr;
				cur.x() += mSpaceAdvance*scale.factor();
				continue;
			}
			
			const auto& glyph = mGlyphs.at(codepoint);

			if (prev && prev->mKerning.count(codepoint))
				cur.x() += prev->mKerning.at(codepoint)*scale.factor();

			result.push_back(make_pair(glyph.mBounds.transformed().translated(cur), glyph.mTextureBounds));

			cur.x() += glyph.mAdvance*scale.factor();
			prev = &glyph;
		}

		if (horizontalAnchor != TextAnchor::eMin) {
			cur = Vector2f(cur.x() - cur.x(), 0);
			if (horizontalAnchor == TextAnchor::eMid) cur.x() /= 2;
			ranges::for_each(result | views::drop(lineStart), [](auto& g){ g.translate(cur); });
		}
	}
};

}