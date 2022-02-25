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

RWTexture2D<float2> gOutput1;
RWTexture2D<float4> gOutput2;
Texture2D<float4> gRadiance;
Texture2D<float4> gPrevRadiance;
Texture2D<uint> gGradientSamples;
StructuredBuffer<ViewData> gViews;
#include "../../visibility_buffer.hlsli"

[[vk::push_constant]] const struct {
	uint gViewCount;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	const uint viewIndex = get_view_index(index.xy*gGradientDownsample, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	const ViewData view = gViews[viewIndex];

	const uint2 gradPos = index.xy;
	const uint u = gGradientSamples[gradPos];
	const uint2 tile_pos = uint2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	const uint2 ipos = gradPos*gGradientDownsample + tile_pos;
	if (!test_inside_screen(ipos, view)) return;
	
	const float l_curr = luminance(gRadiance[ipos].rgb);
	if (u >= (1u << 31u)) {
		const uint idx_prev = (u >> (2u * TILE_OFFSET_SHIFT)) & ((1u << (31u - 2u * TILE_OFFSET_SHIFT)) - 1u);
		const uint width = view.image_max.x - view.image_min.x;
		const uint2 ipos_prev = view.image_min + uint2(idx_prev % width, idx_prev / width);
		const float l_prev = luminance(gPrevRadiance[ipos_prev].rgb);
		gOutput1[index.xy] = (isinf(l_prev) || isnan(l_prev)) ? l_curr : float2(max(l_curr, l_prev), l_curr - l_prev);
	} else
		gOutput1[index.xy] = 0;

	float2 moments = float2(l_curr, l_curr*l_curr);
	float sum_w = 1;
	const VisibilityInfo v = load_visibility(ipos);
	if (v.instance_index() != INVALID_INSTANCE)
		for (int yy = 0; yy < gGradientDownsample; yy++) {
			for (int xx = 0; xx < gGradientDownsample; xx++) {
				const int2 p = index.xy*gGradientDownsample + int2(xx, yy);
				if (all(ipos == p)) continue;
				if (!test_inside_screen(p, view)) continue;
				if (v.instance_index() != load_visibility(p).instance_index()) continue;
				const float l = luminance(gRadiance[p].rgb);
				if (isinf(l) || isnan(l)) continue;
				moments += float2(l, l*l);
				sum_w++;
			}
		}
	moments /= float2(sum_w, sum_w*sum_w);

	gOutput2[index.xy] = float4(moments[0], max(0, moments[1] - moments[0]*moments[0]), v.z(), length(v.dz_dxy()));
}
