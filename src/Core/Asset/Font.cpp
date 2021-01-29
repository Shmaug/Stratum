#include <msdfgen/msdfgen.h>
#include <msdfgen/ext/import-font.h>

#include "Font.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include "../Buffer.hpp"
#include "../CommandBuffer.hpp"
#include "../DescriptorSet.hpp"
#include "../Pipeline.hpp"
#include "Texture.hpp"


using namespace stm;

const string gCharacters = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
const uint32_t gBitmapPadding = 16;
const double gBitmapScale = 8;

void WriteGlyphCache(const fs::path& filename, const unordered_map<uint32_t, msdfgen::Bitmap<float, 4>>& bitmaps) {
	ofstream stream(filename, ios::binary);
	stream << gBitmapPadding;
	stream << gBitmapScale;
	stream << (uint32_t)bitmaps.size();
	for (const auto& kp : bitmaps) {
		stream << kp.first;
		stream << (uint32_t)kp.second.width();
		stream << (uint32_t)kp.second.height();
		stream.write((const char*)(const float*)kp.second, sizeof(float) * 4*kp.second.width() * kp.second.height());
	}
}
void ReadGlyphCache(const fs::path& filename, unordered_map<uint32_t, msdfgen::Bitmap<float, 4>>& bitmaps) {
	ifstream stream(filename, ios::binary);
	if (!stream.is_open()) return;
	uint32_t pad, count;
	double scale;
	stream >> pad;
	stream >> scale;
	stream >> count;
	if (scale != gBitmapScale || pad != gBitmapPadding) return;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t c,w,h;
		stream >> c;
		stream >> w;
		stream >> h;
		if (!stream) return;
		bitmaps.emplace(c, msdfgen::Bitmap<float, 4>(w, h));
		stream.read((char*)(float*)bitmaps.at(c), sizeof(float) * 4*w * h);
		if (!stream && i+1 != count) {
			bitmaps.erase(c);
			return;
		}
	}
	printf("Loaded %s\n", filename.string().c_str());
}

inline float2 barycentrics(const float2& p, const float2& a, const float2& b, const float2& c) {
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
	uint2 mx = (uint2)ceil(max(p0, p1));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2((float)x, (float)y) + .5f, p0, p1, 0);
			if (uv.x < 0 || uv.y < 0 || uv.x + uv.y > 1) continue;
			data[x + y * width] = !data[x + y * width];
		}
}
inline void rasterize(bool* data, uint32_t width, const float2& p0, const float2& p1, const float2& c) {
	uint2 mx = (uint2)ceil(max(max(p0, p1), c));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2((float)x, (float)y) + .5f, p0, p1, c);
			float g = uv.x/2 + uv.y;
			if (g*g < uv.y && uv.x >= 0.0 && uv.y >= 0.0 && uv.x + uv.y <= 1.0)
				data[x + y * width] = !data[x + y * width];
		}
}

Font::Font(Device& device, const fs::path& filename) : Asset(device, filename) {
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	msdfgen::FontHandle* font = msdfgen::loadFont(ft, filename.string().c_str());
	if (!font) throw invalid_argument("failed to load font " + filename.string());

	double spaceAdvance, tabAdvance;
	msdfgen::FontMetrics metrics = {};
	msdfgen::getFontMetrics(metrics, font);
	msdfgen::getFontWhitespaceWidth(spaceAdvance, tabAdvance, font);
	mEmSize = (float)metrics.emSize;
	mSpaceAdvance = (float)spaceAdvance;
	mTabAdvance = (float)tabAdvance;
	mAscent = (float)metrics.ascenderY;
	mDescent = (float)metrics.descenderY;
	mLineSpace = (float)metrics.lineHeight;

	fs::path cacheFile = fs::path(filename).replace_extension("stmb");
	
	uint64_t area = 0;
	unordered_map<uint32_t, msdfgen::Bitmap<float, 4>> bitmaps;
	ReadGlyphCache(cacheFile, bitmaps);
	bool writeCache = false;

	for (char c : gCharacters) {
		double advance;
		msdfgen::Shape shape;
		if (!msdfgen::loadGlyph(shape, font, (uint32_t)c, &advance)) continue;
		auto bounds = shape.getBounds();
		if (shape.edgeCount() == 0) continue;

		Glyph& glyph = mGlyphs[c];
		glyph.mAdvance = (float)advance;
		double kerning;
		for (char c1 : gCharacters)
			if (msdfgen::getKerning(kerning, font, (uint32_t)c1, (uint32_t)c))
				glyph.mKerning.emplace(c1, (float)kerning);
		glyph.mOffset = float2((float)bounds.l, (float)bounds.b);
		glyph.mExtent = float2((float)(bounds.r - bounds.l), (float)(bounds.t - bounds.b));
		glyph.mTextureExtent = { (uint32_t)fabs(glyph.mExtent.x*gBitmapScale), (uint32_t)fabs(glyph.mExtent.y*gBitmapScale) };
		glyph.mTextureOffset = 0;

		if (bitmaps.count(c)) {
			area += bitmaps.at(c).width() * bitmaps.at(c).height();
			continue;
		}
		if (glyph.mTextureExtent.x == 0 || glyph.mTextureExtent.y == 0) continue;
		bitmaps.emplace(c, msdfgen::Bitmap<float, 4>(glyph.mTextureExtent.x + 2*gBitmapPadding, glyph.mTextureExtent.y + 2*gBitmapPadding));
		msdfgen::Bitmap<float, 4>& bitmap = bitmaps.at(c);
		area += bitmap.width() * bitmap.height();

		shape.normalize();
		msdfgen::edgeColoringSimple(shape, 3.0);
		msdfgen::generateMTSDF(bitmap, shape, length(glyph.mExtent), gBitmapScale, -msdfgen::Vector2(bounds.l, bounds.b) + gBitmapPadding/gBitmapScale);
		msdfgen::distanceSignCorrection(bitmap, shape, gBitmapScale, -msdfgen::Vector2(bounds.l, bounds.b) + gBitmapPadding/gBitmapScale);
		writeCache = true;
	}
	
	WriteGlyphCache(cacheFile, bitmaps);

	// Place bitmaps
	
	vk::Extent2D extent = { 0, 32 };
	extent.width = (uint32_t)sqrt((double)area);
	// next power of 2
	extent.width--;
	extent.width |= extent.width >> 1;
	extent.width |= extent.width >> 2;
	extent.width |= extent.width >> 4;
	extent.width |= extent.width >> 8;
	extent.width |= extent.width >> 16;
	extent.width++;

	uint2 cur = 0;
	uint32_t lineHeight = 0;

	for (auto& kp : bitmaps) {
		mGlyphs[kp.first].mTextureOffset = cur + gBitmapPadding;
		cur.x += kp.second.width();
		lineHeight = max(lineHeight, (uint32_t)kp.second.height());
		if (cur.x >= extent.width) {
			cur.x = 0;
			cur.y += lineHeight;
			lineHeight = (uint32_t)kp.second.height();
			mGlyphs[kp.first].mTextureOffset = cur + gBitmapPadding;
			cur.x += kp.second.width();
		}
		if (cur.y + kp.second.height() >= extent.height) extent.height *= 2;
	}

	// Copy bitmap data

	int8_t* data = new int8_t[4*extent.width*extent.height];
	memset(data, 127, 4*extent.width*extent.height);
	for (auto& kp : bitmaps)
		for (uint32_t y = 0; y < (uint32_t)kp.second.height(); y++) {
			int8_t* dst = data + 4*(mGlyphs[kp.first].mTextureOffset.x-gBitmapPadding + (mGlyphs[kp.first].mTextureOffset.y+y-gBitmapPadding)*extent.width);
			for (uint32_t x = 0; x < 4*(uint32_t)kp.second.width(); x++)
				dst[x] = (uint8_t)(fminf(fmaxf(kp.second.operator()(0, y)[x], -1), 1)*127);
		}
	mSDF = make_shared<Texture>(filename.string()+"/SDF", device, vk::Extent3D(extent, 1), vk::Format::eR8G8B8A8Snorm, byte_blob(4*extent.width*extent.height, data), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, 1);
	delete[] data;
	
	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);
}

void Font::GenerateGlyphs(vector<GlyphRect>& result, fAABB& bounds, const string& str, float pixelHeight, const float2& offset, TextAnchor horizontalAnchor) const {	
	if (str.empty()) return;

	const Glyph* prev = nullptr;

	float scale = pixelHeight / mEmSize;

	float baseline = offset.y;
	float currentPoint = offset.x;
	size_t lineStart = result.size();

	bounds.mMax = numeric_limits<float>::lowest();
	bounds.mMin = numeric_limits<float>::max();

	for (uint32_t i = 0; i < str.length(); i++) {
		uint32_t codepoint = (uint32_t)str[i];
		
		if (codepoint == '\n') {
			if (horizontalAnchor == TextAnchor::eMid)
				for (size_t j = lineStart; j < result.size(); j++)
					result[j].Offset.x -= (currentPoint - offset.x) * .5f;
			else if (horizontalAnchor == TextAnchor::eMax)
				for (size_t j = lineStart; j < result.size(); j++)
					result[j].Offset.x -= (currentPoint - offset.x);
			currentPoint = offset.x;
			baseline -= mLineSpace*scale;
			prev = nullptr;
			lineStart = result.size();
			continue;
		}
		if (codepoint == '\t') {
			prev = nullptr;
			currentPoint = ceilf(currentPoint/(mTabAdvance*scale))*mTabAdvance*scale;
			continue;
		}
		if (codepoint == ' ' || mGlyphs.count(codepoint) == 0) {
			prev = nullptr;
			currentPoint += mSpaceAdvance*scale;
			continue;
		}
		
		const Glyph& glyph = mGlyphs.at(codepoint);

		if (prev && prev->mKerning.count(codepoint))
			currentPoint += prev->mKerning.at(codepoint)*scale;
	
		GlyphRect g = {};
		g.Offset = float2(currentPoint, baseline) + glyph.mOffset*scale;
		g.Extent = glyph.mExtent*scale;
		g.TextureST = float4((float2)glyph.mTextureExtent, (float2)glyph.mTextureOffset) / (float4)uint4(mSDF->Extent().width, mSDF->Extent().height, mSDF->Extent().width, mSDF->Extent().height);
		result.push_back(g);

		bounds.mMin = float3(min((float2)bounds.mMin, g.Offset), 0);
		bounds.mMax = float3(max((float2)bounds.mMax, g.Offset), 0);
		bounds.mMin = float3(min((float2)bounds.mMin, g.Offset + g.Extent), 0);
		bounds.mMax = float3(max((float2)bounds.mMax, g.Offset + g.Extent), 0);

		currentPoint += glyph.mAdvance*scale;
		prev = &glyph;
	}

	if (horizontalAnchor == TextAnchor::eMid)
		for (uint32_t i = (uint32_t)lineStart; i < result.size(); i++)
			result[i].Offset.x -= (currentPoint - offset.x) * .5f;
	else if (horizontalAnchor == TextAnchor::eMax)
		for (uint32_t i = (uint32_t)lineStart; i < result.size(); i++)
			result[i].Offset.x -= (currentPoint - offset.x);
}