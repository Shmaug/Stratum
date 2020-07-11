#include <Content/Font.hpp>
#include <Content/AssetManager.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>
#include <Content/Shader.hpp>
#include <Core/Buffer.hpp>

#include <ThirdParty/stb_truetype.h>

#include <cstring>

using namespace std;

Font::Font(const string& name, Device* device, const string& filename, float characterHeight)
	: mName(name), mTexture(nullptr), mCharacterHeight(characterHeight), mLineSpace(0) {

	memset(mGlyphs, 0, sizeof(FontGlyph) * 0xFF);

	string file;
	if (!ReadFile(filename, file)) throw;

	stbtt_fontinfo font;
	int err = stbtt_InitFont(&font, (const unsigned char*)file.data(), 0);
	if (err != 1) {
		fprintf_color(COLOR_RED, stderr, "Error: Failed to load %s (%d)\n", filename.c_str(), err);
		throw;
	}

	float FS = stbtt_ScaleForPixelHeight(&font, characterHeight);

	int ascend, descend, space;
	stbtt_GetFontVMetrics(&font, &ascend, &descend, &space);

	mAscender = ascend * FS;
	mDescender = descend * FS;
	mLineSpace = (-descend + ascend) * FS;

	struct GlyphBitmap {
		unsigned char* data;
		uint32_t glyph;
		VkRect2D rect;
	};
	vector<GlyphBitmap> bitmaps;

	const uint32_t PADDING = 2;

	uint32_t area = 0;
	uint32_t maxWidth = 0;
	uint32_t maxCharSize = 0;

	for (uint32_t c = 0; c < 0xFF; c++) {
		int glyphIndex = stbtt_FindGlyphIndex(&font, c);
		if (glyphIndex <= 0) continue;

		FontGlyph& g = mGlyphs[c];
		g.mCharacter = c;

		int advanceWidth, leftSideBearing;
		stbtt_GetGlyphHMetrics(&font, glyphIndex, &advanceWidth, &leftSideBearing);
		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBox(&font, glyphIndex, FS, FS, &x0, &y0, &x1, &y1);

		int x, y, w, h;
		uint8_t* data = stbtt_GetGlyphBitmap(&font, FS, FS, glyphIndex, &w, &h, &x, &y);
		bitmaps.push_back({ data, c, {{ 0, 0 }, { (uint32_t)w + PADDING, (uint32_t)h + PADDING } } });

		g.mAdvance = advanceWidth * FS;
		g.mOffset = float2(x0, -y0);
		g.mExtent = float2(x1 - x0, y0 - y1);

		area += w * h;
		maxWidth = max(maxWidth, (uint32_t)w);
		maxCharSize = max((uint32_t)min(w, h), maxCharSize);
		
		/*
		if (c == 'e' || c == 'i' || c == 'z') {
			string stem = fs::path(filename).stem().string();
			fprintf(stderr, "%s: ", stem.c_str());
			for (int i = 0; i < w - (int)stem.length(); i++) fprintf(stderr, "%c", '*');
				fprintf(stderr, "\n");

			for (int j = 0; j < h; j++) {
				fprintf(stderr, " ");
				for (int i = 0; i < w; i++)
					fprintf(stderr, "%c", " +#@"[data[i + j*w] / 64]);
				fprintf(stderr, "\n");
			}
		}
		*/

		for (uint32_t c2 = 0; c2 < 0xFF; c2++)
			g.mKerning[c2] = stbtt_GetCodepointKernAdvance(&font, c, c2) * FS;
	}

	// Pack glyph bitmaps

	// sort the boxes for insertion by height, descending
	sort(bitmaps.begin(), bitmaps.end(), [](const GlyphBitmap& a, const GlyphBitmap& b) {
		return b.rect.extent.height < a.rect.extent.height;
	});

	// aim for a squarish resulting container,
	// slightly adjusted for sub-100% space utilization
	// start with a single empty space, unbounded at the bottom
	deque<VkRect2D> spaces {
		{ { PADDING, PADDING }, { max((uint32_t)ceilf(sqrtf(area / 0.95f)), maxWidth), 0xFFFFFFFF } }
	};

	uint2 packedSize(0);

	for (GlyphBitmap& box : bitmaps) {
		// look through spaces backwards so that we check smaller spaces first
		for (int i = (int)spaces.size() - 1; i >= 0; i--) {
			VkRect2D& space = spaces[i];

			// look for empty spaces that can accommodate the current box
			if (box.rect.extent.width > space.extent.width || box.rect.extent.height > space.extent.height) continue;

			// found the space; add the box to its top-left corner
			// |-------|-------|
			// |  box  |       |
			// |_______|       |
			// |         space |
			// |_______________|
			box.rect.offset = space.offset;
			packedSize = max(packedSize, uint2(space.offset.x + box.rect.extent.width, space.offset.y + box.rect.extent.height));

			if (box.rect.extent.width == space.extent.width && box.rect.extent.height == space.extent.height) {
				spaces.erase(spaces.begin() + i);

			} else if (box.rect.extent.height == space.extent.height) {
				// space matches the box height; update it accordingly
				// |-------|---------------|
				// |  box  | updated space |
				// |_______|_______________|
				space.offset.x += box.rect.extent.width;
				space.extent.width -= box.rect.extent.width;

			} else if (box.rect.extent.width == space.extent.width) {
				// space matches the box width; update it accordingly
				// |---------------|
				// |      box      |
				// |_______________|
				// | updated space |
				// |_______________|
				space.offset.y += box.rect.extent.height;
				space.extent.height -= box.rect.extent.height;

			} else {
				// otherwise the box splits the space into two spaces
				// |-------|-----------|
				// |  box  | new space |
				// |_______|___________|
				// | updated space     |
				// |___________________|
				spaces.push_back({
					{ (int32_t)(space.offset.x + box.rect.extent.width), space.offset.y },
					{ space.extent.width - box.rect.extent.width, box.rect.extent.height }
				});
				space.offset.y += box.rect.extent.height;
				space.extent.height -= box.rect.extent.height;
			}
			break;
		}
	}

	// round size up to power of 2
	packedSize.x--;
	packedSize.y--;
	packedSize.x |= packedSize.x >> 1;
	packedSize.y |= packedSize.y >> 1;
	packedSize.x |= packedSize.x >> 2;
	packedSize.y |= packedSize.y >> 2;
	packedSize.x |= packedSize.x >> 4;
	packedSize.y |= packedSize.y >> 4;
	packedSize.x |= packedSize.x >> 8;
	packedSize.y |= packedSize.y >> 8;
	packedSize.x |= packedSize.x >> 16;
	packedSize.y |= packedSize.y >> 16;
	packedSize.x++;
	packedSize.y++;
	
	VkDeviceSize imageSize = packedSize.x * packedSize.y * sizeof(uint8_t);
	uint8_t* pixels = new uint8_t[imageSize];
	memset(pixels, 0, imageSize);

	// copy glyph bitmaps
	float2 packedSizef = float2(packedSize.x, packedSize.y);
	for (GlyphBitmap& p : bitmaps) {
		p.rect.extent.width -= PADDING;
		p.rect.extent.height -= PADDING;
		mGlyphs[p.glyph].mTextureRect = fRect2D(float2(p.rect.offset.x, p.rect.offset.y) / packedSizef, float2(p.rect.extent.width, p.rect.extent.height) / packedSizef);

		for (uint32_t y = 0; y < p.rect.extent.height; y++)
			for (uint32_t x = 0; x < p.rect.extent.width; x++)
				pixels[p.rect.offset.x + x + (p.rect.offset.y + y) * packedSize.x] = p.data[x + y * p.rect.extent.width];
	}

	mTexture = new ::Texture(mName + " Texture", device, pixels, imageSize, { packedSize.x, packedSize.y, 1 }, VK_FORMAT_R8_UNORM, (uint32_t)std::floor(std::log2(maxCharSize)) + 1);

	delete[] pixels;

	for (auto& g : bitmaps)
		stbtt_FreeBitmap(g.data, font.userdata);
}
Font::~Font() {
	safe_delete(mTexture)
}

const FontGlyph* Font::Glyph(uint32_t c) const {
	return mGlyphs[c].mCharacter == c ? &mGlyphs[c] : nullptr;
}
float Font::Kerning(uint32_t from, uint32_t to) const {
	return mGlyphs[from].mKerning[to];
};

uint32_t Font::GenerateGlyphs(const string& str, AABB* aabb, std::vector<TextGlyph>& glyphs, TextAnchor horizontalAnchor, TextAnchor verticalAnchor) const {
	glyphs.resize(str.size());

	float currentPoint = 0;
	float baseline = 0;

	uint32_t lineCount = 0;

	const FontGlyph* prev = nullptr;

	float tabSize = 4;
	if (const FontGlyph* spaceGlyph = Glyph(' ')) tabSize *= spaceGlyph->mAdvance;

	float lineMin = 0;
	float lineMax = 0;

	uint32_t lineStart = 0;
	uint32_t glyphCount = 0;

	auto newLine = [&]() {
		currentPoint = 0;
		baseline -= mLineSpace;
		prev = nullptr;
		lineCount++;

		float x = 0.f;
		switch (horizontalAnchor) {
		case TEXT_ANCHOR_MIN:
			lineMin = 0;
			lineMax = 0;
			return;
		case TEXT_ANCHOR_MID:
			x = (lineMax + lineMin) * .5f;
			break;
		case TEXT_ANCHOR_MAX:
			x = lineMax;
			break;
		}
		for (uint32_t v = lineStart; v < glyphCount; v++)
			glyphs[v].Offset.x -= x;

		lineMin = 0;
		lineMax = 0;
	};

	for (uint32_t i = 0; i < str.length(); i++) {
		if (str[i] == '\n') {
			newLine();
			lineStart = glyphCount;
			continue;
		}
		if (str[i] == '\t') {
			prev = nullptr;
			currentPoint = ceilf(currentPoint / tabSize) * tabSize;
			lineMin = fminf(lineMin, currentPoint);
			lineMax = fmaxf(lineMax, currentPoint);
			continue;
		}
		
		const FontGlyph* glyph = Glyph(str[i]);
		if (!glyph) { prev = nullptr; continue; }

		if (prev) currentPoint += prev->mKerning[str[i]];

		glyphs[glyphCount].Offset = float2(currentPoint, baseline) + glyph->mOffset;
		glyphs[glyphCount].Extent = glyph->mExtent;
		glyphs[glyphCount].TexOffset = glyph->mTextureRect.mOffset;
		glyphs[glyphCount].TexExtent = glyph->mTextureRect.mExtent;

		lineMin = fminf(lineMin, glyphs[glyphCount].Offset.x);
		lineMax = fmaxf(lineMax, glyphs[glyphCount].Offset.x + glyphs[glyphCount].Extent.x);

		glyphCount++;
		currentPoint += glyph->mAdvance;

		prev = glyph;
	}

	newLine();
	float verticalOffset = 0;
	switch (verticalAnchor) {
	case TEXT_ANCHOR_MIN:
		verticalOffset = -(baseline + mLineSpace);
		break;
	case TEXT_ANCHOR_MID:
		verticalOffset = (lineCount * (-mDescender - mLineSpace * .5f) + (lineCount - 1) * mLineSpace) * .5f;
		break;
	case TEXT_ANCHOR_MAX:
		verticalOffset = 0;
		break;
	}
	for (uint32_t i = 0; i < glyphCount; i++)
		glyphs[i].Offset.y += verticalOffset;

	if (aabb) {
		float2 mn = glyphs[0].Offset;
		float2 mx = glyphs[0].Offset + glyphs[0].Extent;
		for (uint32_t i = 1; i < glyphCount; i++) {
			mn = min(mn, glyphs[i].Offset);
			mx = max(mx, glyphs[i].Offset + glyphs[i].Extent);
		}
		*aabb = AABB(float3(mn, 0), float3(mx, 0));
	}
	return glyphCount;
}