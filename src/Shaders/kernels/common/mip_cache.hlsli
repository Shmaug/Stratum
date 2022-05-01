#ifndef PATH_VERTEX_CACHE_H
#define PATH_VERTEX_CACHE_H

RWByteAddressBuffer gPathVertexCache;

inline uint get_hash_index(const int3 p) {
	uint hash_idx = hash_wang(p) % gPushConstants.gPathVertexCacheEntries;
	uint checksum = hash_xorshift(p);
	
	uint steps = 0;
	while (steps < gMaxHashSteps) {
		uint stored;
		gPathVertexCache.InterlockedCompareExchange(4*hash_idx, checksum, 0, stored);
		if (stored == 0 || stored == checksum)
			return hash_idx;
		hash_idx = hash(hash_index) % gPushConstants.gPathVertexCacheEntries;
		steps++;
	}
	return hash_idx;
}

struct PathVertexCacheEntry {
	uint4 data;
};

inline PathVertexCacheEntry get_cache_entry(const uint instance_primitive_index, const float3 pos, const float3 normal, const differential3 d_pos) {
	const int3 v = pos / sqrt(dot(d_pos.dx, d_pos.dx) + dot(d_pos.dy, d_pos.dy));
	const uint packed_normal = pack_normal_octahedron(normal);

	static const uint gPathVertexCacheHeaderSize = gPushConstants.gPathVertexCacheEntries*4;
	static const uint gPathVertexCacheSize = gPushConstants.gPathVertexCacheEntries*16;

	PathVertexCacheEntry e = { gPathVertexCache.Load4(gPathVertexCacheHeaderSize + (addr * 16) % gPathVertexCacheSize); };
	return e;
}

#endif