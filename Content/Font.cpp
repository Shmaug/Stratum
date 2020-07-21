#include <Content/Font.hpp>
#include <Content/AssetManager.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>
#include <Content/Shader.hpp>
#include <Core/Buffer.hpp>

#include <ThirdParty/stb_truetype.h>

#include <cstring>

using namespace std;

#define PADDING 2

struct GlyphBitmap {
	unsigned char* data;
	uint2 resolution;
	VkRect2D rect;
	uint32_t codepoint;
};

float2 barycentrics(const float2& p, const float2& a, const float2& b, const float2& c) {
	float2 v0 = c - a;
	float2 v1 = b - a;
	float2 v2 = p - a;
	float dot00 = dot(v0, v0);
	float dot01 = dot(v0, v1);
	float dot02 = dot(v0, v2);
	float dot11 = dot(v1, v1);
	float dot12 = dot(v1, v2);
	return float2(dot11 * dot02 - dot01 * dot12, dot00 * dot12 - dot01 * dot02) / (dot00 * dot11 - dot01 * dot01);
}
inline void rasterize(bool* data, uint32_t width, const float2& p0, const float2& p1) {
	uint2 mx = uint2(ceil(max(p0, p1)));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2(x, y) + .5f, p0, p1, 0);
			if (uv.x < 0 || uv.y < 0 || uv.x + uv.y > 1) continue;
			data[x + y * width] = !data[x + y * width];
		}
}
inline void rasterize(bool* data, uint32_t width, const float2& p0, const float2& p1, const float2& c) {
	uint2 mx = uint2(ceil(max(max(p0, p1), c)));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2(x, y) + .5f, p0, p1, c);
			float g = uv.x/2 + uv.y;
			if (g*g < uv.y && uv.x >= 0.0 && uv.y >= 0.0 && uv.x + uv.y <= 1.0)
				data[x + y * width] = !data[x + y * width];
		}
}

Font::Font(const string& name, Device* device, const string& filename) : mName(name), mLineSpace(0) {
	string file;
	if (!ReadFile(filename, file)) throw;

	stbtt_fontinfo font;
	int err = stbtt_InitFont(&font, (const unsigned char*)file.data(), 0);
	if (err != 1) {
		fprintf_color(COLOR_RED, stderr, "Error: Failed to load %s (%d)\n", filename.c_str(), err);
		throw;
	}

	stbtt_GetFontVMetrics(&font, &mAscent, &mDescent, nullptr);
	mLineSpace = -mDescent + mAscent;

	mFontHeight = 1.f / stbtt_ScaleForPixelHeight(&font, 1.f);
	
	// Create glyphs and kernings for 0-255

	for (uint32_t c = 0; c < 0xFF; c++) {
		int glyphIndex = stbtt_FindGlyphIndex(&font, c);
		if (glyphIndex <= 0) continue;

		int2 p0, p1;
		stbtt_GetGlyphBitmapBox(&font, glyphIndex, 1, 1, &p0.x, &p0.y, &p1.x, &p1.y);

		swap(p0.y, p1.y);
		p0.y = -p0.y;
		p1.y = -p1.y;

		FontGlyph& g = mGlyphs[c];
		g.mCodepoint = (char)c;
		g.mOffset = p0;
		g.mExtent = p1 - p0;
		stbtt_GetGlyphHMetrics(&font, glyphIndex, &g.mAdvance, nullptr);
		for (uint32_t c2 = 0; c2 < 0xFF; c2++)
			if (int32_t kern = stbtt_GetCodepointKernAdvance(&font, c, c2))
				g.mKerning[c2] = kern;

		stbtt_vertex* vertices;
		int vertexCount = stbtt_GetGlyphShape(&font, glyphIndex, &vertices);
		g.mShape.resize(vertexCount);
		for (uint32_t i = 0; i < (uint32_t)vertexCount; i++) {
			g.mShape[i].Endpoint = int2(vertices[i].x, -vertices[i].y);
			g.mShape[i].ControlPoint = uint2(vertices[i].cx, -vertices[i].cy);
			g.mShape[i].Degree = vertices[i].type - 1;
		}
		stbtt_FreeShape(&font, vertices);
	}
}
Font::~Font() {}

void Font::GenerateGlyphs(const string& str, float pixelHeight, std::vector<TextGlyph>& glyphs, std::vector<GlyphVertex>& vertices, const float2& offset, AABB* aabb, TextAnchor horizontalAnchor) const {	
	if (str.empty()) return;

	const FontGlyph* prev = nullptr;

	float FS = pixelHeight / mFontHeight;

	float baseline = offset.y;
	float currentPoint = offset.x;

	int32_t lineCount = 0;
	float lineMin = currentPoint;
	float lineMax = currentPoint;

	uint32_t baseGlyph = glyphs.size();
	uint32_t lineStart = baseGlyph;

	float tabSize = 4;
	if (const FontGlyph* spaceGlyph = Glyph(' ')) tabSize *= spaceGlyph->mAdvance * FS;

	for (uint32_t i = 0; i < str.length(); i++) {
		if (str[i] == '\n') {
			if (horizontalAnchor == TEXT_ANCHOR_MID) {
				float ox = (lineMax + lineMin) * .5f;
				for (uint32_t i = lineStart; i < glyphs.size(); i++) glyphs[i].Offset.x -= ox;
			} else if (horizontalAnchor == TEXT_ANCHOR_MAX) {
				for (uint32_t i = lineStart; i < glyphs.size(); i++) glyphs[i].Offset.x -= lineMax;
			}
			currentPoint = offset.x;
			baseline -= mLineSpace*FS;
			prev = nullptr;
			lineCount++;
			lineStart = glyphs.size();
			lineMin = currentPoint;
			lineMax = currentPoint;
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

		if (prev && prev->mKerning.count((uint32_t)str[i]))
			currentPoint += prev->mKerning.at((uint32_t)str[i])*FS;
		
		TextGlyph g = {};
		g.StartVertex = vertices.size();
		g.VertexCount = glyph->mShape.size();
		g.Offset = float2(currentPoint, baseline) + float2(glyph->mOffset)*FS;
		g.Extent = float2(glyph->mExtent)*FS;
		g.ShapeOffset = float2(glyph->mOffset)*float2(1,-1);
		g.ShapeExtent = float2(glyph->mExtent);
		g.ShapeExtent.y = -g.ShapeExtent.y;
		glyphs.push_back(g);

		for (uint32_t i = 0; i < glyph->mShape.size(); i++)
			vertices.push_back(glyph->mShape[i]);

		lineMin = fminf(lineMin, currentPoint);
		currentPoint += glyph->mAdvance*FS;
		lineMax = fmaxf(lineMax, currentPoint);
		prev = glyph;
	}

	if (glyphs.size() == baseGlyph) return;

	if (horizontalAnchor == TEXT_ANCHOR_MID)
		for (uint32_t i = lineStart; i < glyphs.size(); i++) glyphs[i].Offset.x -= (lineMax + lineMin) * .5f;
	else if (horizontalAnchor == TEXT_ANCHOR_MAX)
		for (uint32_t i = lineStart; i < glyphs.size(); i++) glyphs[i].Offset.x -= lineMax;

	if (aabb) {
		float2 mn = glyphs[baseGlyph].Offset;
		float2 mx = glyphs[baseGlyph].Offset + glyphs[baseGlyph].Extent;
		for (uint32_t i = baseGlyph + 1; i < glyphs.size(); i++) {
			mn = min(mn, min(glyphs[i].Offset, glyphs[i].Offset + glyphs[i].Extent));
			mx = max(mx, max(glyphs[i].Offset, glyphs[i].Offset + glyphs[i].Extent));
		}
		*aabb = AABB(float3(mn, 0), float3(mx, 0));
	}
}