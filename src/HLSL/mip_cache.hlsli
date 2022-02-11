#ifndef FACE_CACHE_H
#define FACE_CACHE_H

#define gMipCacheLevels 8
#define gMipCacheSize (128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2 + 1*1);

struct MipCacheEntry {
	uint normal;
	float4 data;
};

struct SampleCacheEntry {
	MipCacheEntry mMipCache[gMipCacheSize];

	inline MipCacheEntry get(const float2 uv, const differential2 dUV) {
		const uint level = min(gMipCacheLevels-1, 0.5 + log2(min( width * length(dUV.dx), height * length(dUV.dy) )));
		uint level_offset = 0;
		for (uint i = 0; i < level; i++) {
			uint s = 1 << i;
			level_offset += s*s;
		}
		const uint level_width = 1 << (gMipCacheLevels - level);
		uint2 xy = (uv * level_width) % level_width;
		const uint mip_idx = (xy.x >> level) + (xy.y >> level)*level_width;
		return mMipCache[level_offset + mip_idx];
	}
};

inline SampleCacheEntry get(RWStructuredBuffer<SampleCacheEntry> cache, uint mesh_id) {
	return cache[mesh_id]
}

#endif