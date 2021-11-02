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

#pragma compile dxc -spirv -T cs_6_7 -E main

#include "svgf_shared.hlsli"

[[vk::binding(0)]] RWTexture2D<float2> gOutput1;
[[vk::binding(1)]] RWTexture2D<float4> gOutput2;
[[vk::binding(2)]] Texture2D<float4> gRadiance;
[[vk::binding(3)]] Texture2D<float4> gPrevRadiance;
[[vk::binding(4)]] Texture2D<uint> gGradientPositions;
[[vk::binding(5)]] Texture2D<float4> gZ;
[[vk::binding(6)]] Texture2D<uint4> gVisibility;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 resolution;
	gRadiance.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution/gGradientDownsample)) return;

	float2 o1 = 0;

	uint2 gradPos = index.xy;
	uint u = gGradientPositions[gradPos];
	uint2 tile_pos = uint2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	uint2 ipos = gradPos*gGradientDownsample + tile_pos;
	float l_curr = luminance(gRadiance[ipos].rgb);
	if (u >= (1u << 31u)) {
		uint idx_prev = (u >> (2u * TILE_OFFSET_SHIFT)) & ((1u << (31u - 2u * TILE_OFFSET_SHIFT)) - 1u);
		uint2 ipos_prev = uint2(idx_prev % resolution.x, idx_prev / resolution.x);
		float l_prev = luminance(gPrevRadiance[ipos_prev].rgb);
		//if (isinf(l_prev) || isnan(l_prev)) l_prev = 0;
		o1 = float2(max(l_curr, l_prev), l_curr - l_prev);
	}

	float2 moments = float2(l_curr, l_curr*l_curr);
	float sum_w = 1;
	uint mesh_id = gVisibility[ipos].x;
	if (mesh_id != -1)
		for (int yy = 0; yy < gGradientDownsample; yy++) {
			for (int xx = 0; xx < gGradientDownsample; xx++) {
				int2 p = index.xy*gGradientDownsample + int2(xx, yy);
				if (all(ipos == p)) continue;
				if (gVisibility[p].x != mesh_id) continue;
				float l = luminance(gRadiance[p].rgb);
				if (isinf(l) || isnan(l)) continue;
				moments += float2(l, l*l);
				sum_w++;
			}
		}
	moments /= sum_w;

	gOutput1[index.xy] = o1;
	gOutput2[index.xy] = float4(moments[0], max(0, moments[1] - moments[0]*moments[0]), gZ[ipos].xy);
}
