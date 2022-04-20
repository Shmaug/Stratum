#ifndef BITFIELD_H
#define BITFIELD_H

// https://gist.github.com/Jeff-Russ/c9b471158fa7427280e6707d9b11d7d2

/* Bit Manipulation Macros
A good article: http://www.coranac.com/documents/working-with-bits-and-bitfields/
x    is a variable that will be modified.
y    will not.
pos  is a unsigned int (usually 0 through 7) representing a single bit position where the
     right-most bit is bit 0. So 00000010 is pos 1 since the second bit is high.
bm   (bit mask) is used to specify multiple bits by having each set ON.
bf   (bit field) is similar (it is in fact used as a bit mask) but it is used to specify a
     range of neighboring bit by having them set ON.
*/
/* shifts left the '1' over pos times to create a single HIGH bit at location pos. */
#define BIT(pos) ( 1u << (pos) )

/* Set single bit at pos to '1' by generating a mask
in the proper bit location and ORing x with the mask. */
#define SET_BIT(x, pos) ( (x) |= (BIT(pos)) )
#define SET_BITS(x, bm) ( (x) |= (bm) ) // same but for multiple bits

/* Set single bit at pos to '0' by generating a mask
in the proper bit location and ORing x with the mask. */
#define UNSET_BIT(x, pos) ( (x) &= ~(BIT(pos)) )
#define UNSET_BITS(x, bm) ( (x) &= (~(bm)) ) // same but for multiple bits

/* Set single bit at pos to opposite of what is currently is by generating a mask
in the proper bit location and ORing x with the mask. */
#define FLIP_BIT(x, pos) ( (x) ^= (BIT(pos)) )
#define FLIP_BITS(x, bm) ( (x) ^= (bm) ) // same but for multiple bits

/* Return '1' if the bit value at position pos within y is '1' and '0' if it's 0 by
ANDing x with a bit mask where the bit in pos's position is '1' and '0' elsewhere and
comparing it to all 0's.  Returns '1' in least significant bit position if the value
of the bit is '1', '0' if it was '0'. */
#define CHECK_BIT(y, pos) ( ( 0u == ( (y)&(BIT(pos)) ) ) ? 0u : 1u )
#define CHECK_BITS_ANY(y, bm) ( ( (y) & (bm) ) ? 0u : 1u )
// warning: evaluates bm twice:
#define CHECK_BITS_ALL(y, bm) ( ( (bm) == ((y)&(bm)) ) ? 0u : 1u )

// These are three preparatory macros used by the following two:
#define SET_LSBITS(len) ( (1u << (len)) - 1u ) // the first len bits are '1' and the rest are '0'
#define BF_MASK(start, len) ( SET_LSBITS(len) << (start) ) // same but with offset
#define BF_PREP(y, start, len) ( ((y)&SET_LSBITS(len)) << (start) ) // Prepare a bitmask

/* Extract a bitfield of length len starting at bit start from y. */
#define BF_GET(y, start, len) ( ((y) >> (start)) & SET_LSBITS(len) )

/* Insert a new bitfield value bf into x. */
#define BF_SET(x, bf, start, len) ( x = ((x) &~ BF_MASK(start, len)) | BF_PREP(bf, start, len) )

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define BF_GET_UNORM(y, start, len) ( BF_GET(y, start, len) / (float)SET_LSBITS(len) )
#define BF_SET_UNORM(x, f, start, len) BF_SET(x, (uint)(saturate(f) * (float)SET_LSBITS(len)), start, len)

#ifdef __HLSL_VERSION

inline uint pack_f16_2(const float2 v) {
	const uint2 f16 = f32tof16(v);
	return f16.x | (f16.y << 16);
}
inline uint2 pack_f16_4(const float4 v) {
	const uint4 f16 = f32tof16(v);
	return uint2(f16.x | (f16.y << 16), (f16.z << 0)  | (f16.w << 16));
}
inline min16float2 unpack_f16_2(const uint v) {
	return (min16float2)f16tof32(uint2(v, v >> 16));
}
inline min16float4 unpack_f16_4(const uint2 v) {
	return min16float4(f16tof32(uint2(v.x, v.x >> 16)), f16tof32(uint2(v.y, v.y >> 16)));
}

inline float2 pack_normal_octahedron2(const float3 v) {
	// Project the sphere onto the octahedron, and then onto the xy plane
	const float2 p = v.xy * (1 / (abs(v.x) + abs(v.y) + abs(v.z)));
	// Reflect the folds of the lower hemisphere over the diagonals
	return (v.z <= 0) ? ((1 - abs(p.yx)) * lerp(-1, 1, p >= 0)) : p;
}
inline float3 unpack_normal_octahedron2(const float2 p) {
	float3 v = float3(p, 1 - dot(1, abs(p)));
	if (v.z < 0) v.xy = (1 - abs(v.yx)) * lerp(-1, 1, v.xy >= 0);
	return normalize(v);
}

inline uint pack_normal_octahedron(const float3 v) {
	return pack_f16_2(pack_normal_octahedron2(v));
}
inline float3 unpack_normal_octahedron(const uint packed) {
	return unpack_normal_octahedron2(unpack_f16_2(packed));
}

#endif

#endif