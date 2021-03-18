#include <msdfgen/msdfgen.h>
#include <msdfgen/ext/import-font.h>

#include "Font.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "DescriptorSet.hpp"
#include "Pipeline.hpp"
#include "Texture.hpp"


using namespace stm;

const string gCharacters = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
const uint32_t gBitmapPadding = 16;
const float gBitmapScale = 8;

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

Font::Font(CommandBuffer& commandBuffer, const fs::path& filename) {
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
		glyph.mBounds = AlignedBox2f(Vector2f(bounds.l, bounds.b), Vector2f(bounds.r, bounds.t));
		glyph.mTextureBounds = AlignedBox2i(Vector2i::Zero(), (glyph.mBounds.sizes()*gBitmapScale).cast<int32_t>());

		if (bitmaps.count(c)) {
			area += bitmaps.at(c).width() * bitmaps.at(c).height();
			continue;
		}
		if (glyph.mTextureBounds.isEmpty()) continue;

		// render bitmap

		Vector2i bitmapSize = glyph.mTextureBounds.sizes() + Vector2i::Constant(2*gBitmapPadding);
		auto& bitmap = bitmaps.emplace(c, msdfgen::Bitmap<float, 4>((uint32_t)bitmapSize.x(), (uint32_t)bitmapSize.y())).first->second;
		area += bitmapSize.prod();

		shape.normalize();
		msdfgen::edgeColoringSimple(shape, 3.0);
		msdfgen::generateMTSDF(bitmap, shape, glyph.mTextureBounds.sizes().norm(), gBitmapScale, -msdfgen::Vector2(bounds.l, bounds.b) + gBitmapPadding/gBitmapScale);
		msdfgen::distanceSignCorrection(bitmap, shape, gBitmapScale, -msdfgen::Vector2(bounds.l, bounds.b) + gBitmapPadding/gBitmapScale);
		writeCache = true;
	}
	
	WriteGlyphCache(cacheFile, bitmaps);

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

	// Place bitmaps row-by-row
	Vector2i cur = Vector2i::Zero();
	uint32_t lineHeight = 0;
	for (auto& [codepoint, glyphImg] : bitmaps) {
		Vector2i dst = cur;
		cur.x() += glyphImg.width();
		if ((uint32_t)cur.x() >= extent.width) {
			// next row
			cur.x() = 0;
			cur.y() += lineHeight;
			lineHeight = (uint32_t)glyphImg.height();
			dst = cur;
			cur.x() += glyphImg.width();
		} else
			lineHeight = max(lineHeight, (uint32_t)glyphImg.height());
		while ((uint32_t)cur.y() + glyphImg.height() >= extent.height) extent.height *= 2;
		mGlyphs[codepoint].mTextureBounds.translate(dst + Vector2i::Constant(gBitmapPadding));
	}

	// Copy bitmap data

	vector<int8_t> data(4*extent.width*extent.height, 0xFF/2);
	for (auto& [idx,img] : bitmaps)
		for (uint32_t y = 0; y < (uint32_t)img.height(); y++) {
			int8_t* dst = data.data() + 4*(mGlyphs[idx].mTextureBounds.min().x()-gBitmapPadding + (mGlyphs[idx].mTextureBounds.min().y()+y-gBitmapPadding)*extent.width);
			for (uint32_t x = 0; x < 4*(uint32_t)img.width(); x++)
				dst[x] = (uint8_t)(fminf(fmaxf(img.operator()(0, y)[x], -1), 1)*127);
		}

	mSDF = make_shared<Texture>(commandBuffer.mDevice, filename.string()+"_msdf", vk::Extent3D(extent, 1), vk::Format::eR8G8B8A8Snorm, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, 1);
	mSDF->TransitionBarrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
	commandBuffer->copyBufferToImage(*commandBuffer.UploadData(data).buffer(), **mSDF, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy() });

	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);
}