#ifndef CMJ_H
#define CMJ_H

#define CMJ_DIM 16

uint permute(uint i, uint l, uint p) {
	uint w = l - 1;
	w |= w >> 1;
	w |= w >> 2;
	w |= w >> 4;
	w |= w >> 8;
	w |= w >> 16;
	do {
		i ^= p;
		i *= 0xe170893d;
		i ^= p >> 16;
		i ^= (i & w) >> 4;
		i ^= p >> 8;
		i *= 0x0929eb3f;
		i ^= p >> 23;
		i ^= (i & w) >> 1;
		i *= 1 | p >> 27;
		i *= 0x6935fa69;
		i ^= (i & w) >> 11;
		i *= 0x74dcb303;
		i ^= (i & w) >> 2;
		i *= 0x9e501cc3;
		i ^= (i & w) >> 2;
		i *= 0xc860a3df;
		i &= w;
		i ^= i >> 5;
	} while (i >= l);
	return (i + p) % l;
}
float randfloat(uint i, uint p) {
	i ^= p;
	i ^= i >> 17;
	i ^= i >> 10;
	i *= 0xb36534e5;
	i ^= i >> 12;
	i ^= i >> 21;
	i *= 0x93fc4795;
	i ^= 0xdf6e307f;
	i ^= i >> 17;
	i *= 1 | p >> 18;
	return i / 4294967808;
}

class SamplerCMJ {
	uint index;
	uint dimension;
	uint scramble;
	uint pad;

	float2 Sample() {
		uint p = dimension * scramble;
		dimension++;
		uint idx = permute(index, CMJ_DIM * CMJ_DIM, 0xa399d265 * p);
		uint sx = permute(idx % CMJ_DIM, CMJ_DIM, p * 0xa511e9b3);
		uint sy = permute(idx / CMJ_DIM, CMJ_DIM, p * 0x63d83595);
		float jx = randfloat(idx, p * 0xa399d265);
		float jy = randfloat(idx, p * 0x711ad6a5);
		return frac(float2((idx % CMJ_DIM + (sy + jx) / CMJ_DIM) / CMJ_DIM, (idx / CMJ_DIM + (sx + jy) / CMJ_DIM) / CMJ_DIM));
	}
};

float2 MapToDisk(float2 uv) {
	float theta = 2*M_PI*uv.y;
	return sqrt(uv.x) * float2(cos(theta), sin(theta));
}
float2 MapToDiskConcentric(float2 uv) {
	float2 offset = 2*uv - 1;
	if (all(offset == 0)) return 0;
	float theta, r;
	if (abs(offset.x) > abs(offset.y)) {
		r = offset.x;
		theta = M_PI/4 * (offset.y/offset.x);
	} else {
		r = offset.y;
		theta = M_PI/2 * (1 - (offset.x/offset.y)/2);
	}
	return r * float2(cos(theta), sin(theta));
}
float3 MapToHemisphere(float2 uv) {
	float phi = 2*M_PI*uv.y;
	float cosTheta = sqrt(uv.x);
	float sinTheta = sqrt(1 - cosTheta*cosTheta);
	return float3(cos(phi)*sinTheta, cosTheta, sin(phi)*sinTheta);
}
float3 MapToSphere(float2 uv) {
	float theta = uv.x*2-1;
	float phi = 2*M_PI*uv.y;
	float sinTheta = sin(theta);
	return float3(cos(phi)*sinTheta, cos(theta), sin(phi)*sinTheta);
}

#endif