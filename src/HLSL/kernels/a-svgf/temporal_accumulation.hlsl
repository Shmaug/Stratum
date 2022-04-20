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

[[vk::constant_id(0)]] const uint gGradientFilterRadius = 0;
[[vk::constant_id(1)]] const bool gUseVisibility = true;

#define PT_DESCRIPTOR_SET_1
#include "../../pt_descriptors.hlsli"
#include "svgf_common.hlsli"

[[vk::push_constant]] const struct {
	uint gViewCount;
	float gHistoryLimit;
	float gAntilagScale;
	uint gGradientDownsample;
	uint gAtrousGradientIterations;
} gPushConstants;

#define gDiffImage gDiffImage1[gPushConstants.gAtrousGradientIterations%2]

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	float2 extent;
	gAccumColor.GetDimensions(extent.x, extent.y);

  	const uint2 ipos = index.xy;
	const uint ipos_1d = ipos.y*extent.x + ipos.x;
	const float2 pos_prev = gViews[view_index].image_min + gVisibility[ipos_1d].prev_uv * float2(gViews[view_index].image_max - gViews[view_index].image_min);
	
	const int2 p = pos_prev - 0.5;
	const float2 w = frac(pos_prev - 0.5);

	float4 color_prev   = 0;
	float2 moments_prev = 0;
	float sum_w         = 0;
	if (gUseVisibility) {
		// bilinear interpolation, check each tap individually, renormalize afterwards
		for (int yy = 0; yy <= 1; yy++) {
			for (int xx = 0; xx <= 1; xx++) {
				const int2 ipos_prev = p + int2(xx, yy);
				if (!test_inside_screen(ipos_prev, gViews[view_index])) continue;
				const uint ipos_prev_1d = ipos_prev.y*extent.x + ipos_prev.x;

				if (gInstanceIndexMap[gPrevVisibility[ipos_prev_1d].instance_index()] != gVisibility[ipos_1d].instance_index()) continue;
				if (!test_reprojected_normal(gVisibility[ipos_1d].normal(), gPrevVisibility[ipos_prev_1d].normal())) continue;
				if (!test_reprojected_depth(gVisibility[ipos_1d].prev_z(), gPrevVisibility[ipos_prev_1d].z(), 1, gPrevVisibility[ipos_prev_1d].dz_dxy())) continue;

				const float4 c = gPrevAccumColor[ipos_prev];

				if (c.a <= 0 || any(isnan(c))) continue;	

				const float wc = (xx == 0 ? (1 - w.x) : w.x) * (yy == 0 ? (1 - w.y) : w.y);
				color_prev   += c * wc;
				moments_prev += gPrevAccumMoments[ipos_prev] * wc;
				sum_w        += wc;
			}
		}
	} else if (all(gVisibility[ipos_1d].prev_uv >= 0) && all(gVisibility[ipos_1d].prev_uv < 1) && !isinf(gVisibility[ipos_1d].z())) {
		color_prev = gPrevAccumColor[pos_prev];
		if (any(isnan(color_prev.rgb)) || any(isinf(color_prev.rgb)))
			color_prev = 0;
		moments_prev = gPrevAccumMoments[pos_prev];
		sum_w = 1;
	}

	float4 color_curr = gRadiance[ipos];
	if (any(isnan(color_curr.rgb)) || any(isinf(color_curr.rgb)))
		color_curr = 0;
	const float l = luminance(color_curr.rgb);
	const float2 moments_curr = float2(l, l*l);

	if (gPushConstants.gHistoryLimit > 0 && sum_w > 0 && color_prev.a > 0) {
		const float invSum = 1/sum_w;
		color_prev   *= invSum;
		moments_prev *= invSum;

		float n = min(gPushConstants.gHistoryLimit, color_prev.a + color_curr.a);

		if (gPushConstants.gAntilagScale > 0) {
			float antilag_alpha = 0;
			if (gGradientFilterRadius == 0) {
				const float2 v = gDiffImage[ipos/gPushConstants.gGradientDownsample];
				antilag_alpha = saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0);
			} else {
				for (int yy = -gGradientFilterRadius; yy <= gGradientFilterRadius; yy++)
					for (int xx = -gGradientFilterRadius; xx <= gGradientFilterRadius; xx++) {
						const int2 p = int2(ipos/gPushConstants.gGradientDownsample) + int2(xx, yy);
						if (!test_inside_screen(p*gPushConstants.gGradientDownsample, gViews[view_index])) continue;
						const float2 v = gDiffImage[p];
						antilag_alpha = max(antilag_alpha, saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0));
					}
			}
			antilag_alpha = saturate(antilag_alpha*gPushConstants.gAntilagScale);
			if (!isnan(antilag_alpha) && !isinf(antilag_alpha))
				n = lerp(n, color_curr.a, antilag_alpha);
		}

		const float alpha = saturate(color_curr.a / n);

		gAccumColor[ipos] = float4(lerp(color_prev.rgb, color_curr.rgb, alpha), n);
		gAccumMoments[ipos] = lerp(moments_prev, moments_curr, max(0.6, alpha));
	} else {
		gAccumColor[ipos] = color_curr;
		gAccumMoments[ipos] = moments_curr;
	}
}