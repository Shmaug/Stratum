/*
Copyright (c) 2018, Christoph Schied
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Karlsruhe Institute of Technology nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

float luminance(in float3 color) {
	return dot(color, float3(0.299, 0.587, 0.114));
}

float3 rgb_to_yuv(float3 rgb) {
	return mul(float3x3(
		0.299, -0.14713,  0.615,
		0.587, -0.28886, -0.51499,
		0.114,  0.436,   -0.10001), rgb);
}

float3 yuv_to_rgb(float3 yuv) {
	return mul(float3x3(
		1.0,      1.0,     1.0,
		0.0,     -0.39465, 2.03211,
		1.13983, -0.58060, 0.0), yuv);
}

bool test_reprojected_normal(float3 n1, float3 n2) {
	return dot(n1, n2) > 0.95;
}

bool test_inside_screen(int2 p, int2 res) {
	return all(p >= 0) && all(p < res);
}

bool test_reprojected_depth(float z1, float z2, float dz) {
	float z_diff = abs(z1 - z2);
	return z_diff < 2.0 * (dz + 1e-3);
}

#define TILE_OFFSET_SHIFT 3u
#define TILE_OFFSET_MASK ((1u << TILE_OFFSET_SHIFT) - 1u)

int2 get_gradient_tile_pos(uint idx, int gradientDownsample) {
	/* didn't store a gradient sample in the previous frame, this creates
	   a new sample in the center of the tile */
	if(idx < (1u<<31))
		return gradientDownsample / 2;

	return int2((idx & TILE_OFFSET_MASK), (idx >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
}

uint get_gradient_idx_from_tile_pos(int2 pos) {
	return (1 << 31) | (pos.x) | (pos.y << TILE_OFFSET_SHIFT);
}

bool is_gradient_sample(Texture2D<float> tex_gradient, int2 ipos, int gradientDownsample) {
	int2 ipos_grad = ipos / gradientDownsample;
	uint u = tex_gradient.Load(int3(ipos_grad,0)).r;

	int2 tile_pos = int2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	return u >= (1u << 31) && all(ipos == ipos_grad * gradientDownsample + tile_pos);
 }

