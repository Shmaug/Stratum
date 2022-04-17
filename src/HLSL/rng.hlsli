#ifndef RNG_H
#define RNG_H

uint4 pcg4d(uint4 v) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v = v ^ (v >> 16u);
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}
uint rng_next_uint(const uint index_1d) {
	gPathState.rng_state.w++;
	return pcg4d(gPathState.rng_state).x;
}
float rng_next_float(const uint index_1d) {
    return asfloat(0x3f800000 | (rng_next_uint(index_1d) >> 9)) - 1;
}

#endif