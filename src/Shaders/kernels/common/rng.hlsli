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
uint pcg_next_uint(inout uint4 v) {
	v.w++;
	return pcg4d(v).x;
}
float pcg_next_float(inout uint4 v) {
    return asfloat(0x3f800000 | (pcg_next_uint(v) >> 9)) - 1;
}

#endif