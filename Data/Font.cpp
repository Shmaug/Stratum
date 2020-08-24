#include <ThirdParty/msdfgen/msdfgen.h>
#include <ThirdParty/msdfgen/ext/import-font.h>

#include <Core/Buffer.hpp>
#include <Core/CommandBuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/Pipeline.hpp>
#include <Data/AssetManager.hpp>
#include <Data/Font.hpp>
#include <Data/Texture.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <ThirdParty/stb_image.h>
#include <ThirdParty/stb_image_write.h>

using namespace std;

const string gSampleText = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
const uint32_t gBitmapPadding = 16;
const double gBitmapScale = 8;

void WriteGlyphCache(const string& filename, const unordered_map<uint32_t, msdfgen::Bitmap<float, 4>>& bitmaps) {
	ofstream file(filename, ios::binary);
	WriteValue<uint32_t>(file, gBitmapPadding);
	WriteValue<double>(file, gBitmapScale);
	WriteValue<uint32_t>(file, (uint32_t)bitmaps.size());
	for (const auto& kp : bitmaps) {
		WriteValue<uint32_t>(file, kp.first);
		WriteValue<uint32_t>(file, (uint32_t)kp.second.width());
		WriteValue<uint32_t>(file, (uint32_t)kp.second.height());
		file.write((const char*)(const float*)kp.second, sizeof(float) * 4*kp.second.width() * kp.second.height());
	}
}
void ReadGlyphCache(const string& filename, unordered_map<uint32_t, msdfgen::Bitmap<float, 4>>& bitmaps) {
	ifstream file(filename, ios::binary);
	if (!file.is_open()) return;
	uint32_t pad, count;
	double scale;
	ReadValue<uint32_t>(file, pad);
	ReadValue<double>(file, scale);
	ReadValue<uint32_t>(file, count);
	if (scale != gBitmapScale || pad != gBitmapPadding) return;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t c,w,h;
		ReadValue<uint32_t>(file, c);
		ReadValue<uint32_t>(file, w);
		ReadValue<uint32_t>(file, h);
		if (!file) return;
		bitmaps.emplace(c, msdfgen::Bitmap<float, 4>(w, h));
		file.read((char*)(float*)bitmaps.at(c), sizeof(float) * 4*w * h);
		if (!file && i+1 != count) {
			bitmaps.erase(c);
			return;
		}
	}
	printf("Loaded %s\n", filename.c_str());
}

msdfgen::Point2 msdfpt(const double2& p) {
	return msdfgen::Point2(p.x, p.y);
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
	uint2 mx = uint2(ceil(max(p0, p1)));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2((float)x, (float)y) + .5f, p0, p1, 0);
			if (uv.x < 0 || uv.y < 0 || uv.x + uv.y > 1) continue;
			data[x + y * width] = !data[x + y * width];
		}
}
inline void rasterize(bool* data, uint32_t width, const float2& p0, const float2& p1, const float2& c) {
	uint2 mx = uint2(ceil(max(max(p0, p1), c)));
	float2 uv;
	for (uint32_t y = 0; y < mx.y; y++) 
		for (uint32_t x = 0; x < mx.x; x++) {
			uv = barycentrics(float2((float)x, (float)y) + .5f, p0, p1, c);
			float g = uv.x/2 + uv.y;
			if (g*g < uv.y && uv.x >= 0.0 && uv.y >= 0.0 && uv.x + uv.y <= 1.0)
				data[x + y * width] = !data[x + y * width];
		}
}

Font::Font(const string& name, Device* device, const string& filename) : mName(name) {
	msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
	msdfgen::FontHandle* font = msdfgen::loadFont(ft, filename.c_str());
	if (!font) throw;

	double spaceAdvance, tabAdvance;
	msdfgen::FontMetrics metrics = {};
	msdfgen::getFontMetrics(metrics, font);
	msdfgen::getFontWhitespaceWidth(spaceAdvance, tabAdvance, font);
	mEmSize = (float)metrics.emSize;
	mSpaceAdvance = (float)spaceAdvance;
	mTabAdvance = (float)tabAdvance;
	mAscent = (float)metrics.ascenderY;
	mDescent = (float)metrics.descenderY;
	mLineGap = (float)metrics.lineHeight;

	string cacheFolder = "cache/";
	bool rmcache = false;

	for (auto arg : device->Instance()->CommandLineArguments())
		if (arg == "--rmcache") rmcache = true;

	string cacheFile = cacheFolder + fs::path(filename).filename().replace_extension("stmb").string();
	
	uint64_t area = 0;
	unordered_map<uint32_t, msdfgen::Bitmap<float, 4>> bitmaps;
	ReadGlyphCache(cacheFile, bitmaps);
	bool writeCache = false;

	for (char c : gSampleText) {
		double advance;
		msdfgen::Shape shape;
		if (!msdfgen::loadGlyph(shape, font, (uint32_t)c, &advance)) continue;
		auto bounds = shape.getBounds();
		if (shape.edgeCount() == 0) continue;

		Glyph& glyph = mGlyphs[c];
		glyph.mAdvance = (float)advance;
		double kerning;
		for (char c1 : gSampleText)
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
	
	if (fs::exists(cacheFolder) && rmcache) fs::remove(cacheFolder);
	else if (!rmcache) fs::create_directory(cacheFolder);
	if (!rmcache) WriteGlyphCache(cacheFile, bitmaps);

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
	memset(data, 0x81, 4*extent.width*extent.height);

	for (auto& kp : bitmaps)
		for (uint32_t y = 0; y < (uint32_t)kp.second.height(); y++) {
			int8_t* dst = data + 4*(mGlyphs[kp.first].mTextureOffset.x-gBitmapPadding + (mGlyphs[kp.first].mTextureOffset.y+y-gBitmapPadding)*extent.width);
			for (uint32_t x = 0; x < 4*(uint32_t)kp.second.width(); x++)
				dst[x] = (uint8_t)(fminf(fmaxf(kp.second.operator()(0, y)[x], -1), 1)*127);
		}

	mSDF = new Texture(mName, device, data, 4*extent.width*extent.height, extent, vk::Format::eR8G8B8A8Snorm);
	delete[] data;

	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);
}
Font::~Font() {
	safe_delete(mSDF);
}

void Font::GenerateGlyphs(vector<GlyphRect>& result, AABB& bounds, const string& str, float pixelHeight, const float2& offset, TextAnchor horizontalAnchor) const {	
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
			baseline -= mLineGap*scale;
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
		g.TextureST = float4(float2(glyph.mTextureExtent), float2(glyph.mTextureOffset)) / float4((float)mSDF->Extent().width, (float)mSDF->Extent().height, (float)mSDF->Extent().width, (float)mSDF->Extent().height);
		result.push_back(g);

		bounds.mMin = float3(min(bounds.mMin.xy, g.Offset), 0);
		bounds.mMax = float3(max(bounds.mMax.xy, g.Offset), 0);
		bounds.mMin = float3(min(bounds.mMin.xy, g.Offset + g.Extent), 0);
		bounds.mMax = float3(max(bounds.mMax.xy, g.Offset + g.Extent), 0);

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