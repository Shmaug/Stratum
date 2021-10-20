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

#pragma compile glslc -fshader-stage=comp -fentry-point=main

#include "svgf_shared.hlsli"

RWTexture2D<float4> gOutput1;
RWTexture2D<float4> gOutput2;
Texture2D<float4> gSamples;
Texture2D<uint> gGradientSamples;
Texture2D<float2> gZ;
Texture2D<float4> gNormalId;
Texture2D<float4> gPrevSamples;

[[numthreads(8,8,1)]]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 resolution;
	gOutput1.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy > resolution)) return;

	float4 frag_color1 = 0;
	uint u = gGradientSamples[index];
	int2 tile_pos = int2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	int2 ipos = index.xy + tile_pos;
	float l_curr = luminance(gSamples[int3(ipos,0)].rgb);
	if (u >= (1u << 31)) {
		uint idx_prev = (u >> (2 * TILE_OFFSET_SHIFT)) & ((1 << (31 - 2 * TILE_OFFSET_SHIFT)) - 1);
		uint w,h;
		gSamples.GetDimensions(w,h);
		int2 ipos_prev = int2(idx_prev % w, idx_prev / w);
		float l_prev = luminance(gPrevSamples[ipos_prev].rgb);
		gOutput1[index] = float4(max(l_curr, l_prev), l_curr - l_prev, 1, 0);
	} else 
		gOutput1[index] = 0;

	float2 moments = float2(l_curr, l_curr * l_curr);
	float sum_w = 1.0;
	uint mesh_id  = asuint(gNormalId[ipos].w);
	for (int yy = 0; yy < 1; yy++) {
		for (int xx = 0; xx < 1; xx++) {
			int2 p = index.xy + int2(xx, yy);
			if (any(ipos != p)) {
				float3 rgb = gSamples[int3(p,0)].rgb;
				uint mesh_id_p = asuint(gNormalId[int3(p,0)].w);
				float l = luminance(rgb);
				float w = mesh_id_p == mesh_id ? 1 : 0;
				moments += float2(l, l * l) * w;
				sum_w += w;
			}
		}
	}
	moments /= sum_w;
	float variance = max(0.0, moments[1] - moments[0] * moments[0]);
	float2 z_curr = gZ[ipos];
	gOutput2[index] = float4(moments[0], variance, z_curr);
}
