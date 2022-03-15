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

[[vk::constant_id(1)]] const uint gGradientDownsample = 3u;
[[vk::constant_id(2)]] const int gGradientFilterRadius = 0;
[[vk::constant_id(3)]] const bool gAntilag = true;
[[vk::constant_id(4)]] const float gDisableRejection = false;

RWTexture2D<float4> gAccumColor;
RWTexture2D<float2> gAccumMoments;

Texture2D<float2> gPrevMoments;
Texture2D<float2> gDiff;

Texture2D<float4> gSamples;
Texture2D<float4> gHistory;

StructuredBuffer<ViewData> gViews;
StructuredBuffer<uint> gInstanceIndexMap;

SamplerState gSampler;

#include "../../visibility_buffer.hlsli"

[[vk::push_constant]] const struct {
	uint gViewCount;
	float gHistoryLimit;
	float gAntilagScale;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	const ViewData view = gViews[viewIndex];

  const uint2 ipos = index.xy;
	const VisibilityInfo vis_curr = load_visibility(ipos);

	const float2 pos_prev = view.image_min + vis_curr.prev_uv() * float2(view.image_max - view.image_min) - 0.5;

	const int2 p = pos_prev;
	const float2 w = frac(pos_prev);

	float4 color_prev   = 0;
	float2 moments_prev = 0;
	float sum_w         = 0;
	// bilinear interpolation, check each tap individually, renormalize afterwards
	for (int yy = 0; yy <= 1; yy++)
		for (int xx = 0; xx <= 1; xx++) {
			const int2 ipos_prev = p + int2(xx, yy);
			if (!test_inside_screen(ipos_prev, view)) continue;

			const float4 c = gHistory[ipos_prev];

			if (!gDisableRejection) {
				const VisibilityInfo vis_prev = load_prev_visibility(ipos_prev, gInstanceIndexMap);
				if (vis_prev.instance_index() != vis_curr.instance_index()) continue;
				if (!test_reprojected_normal(vis_curr.normal(), vis_prev.normal())) continue;
				if (!test_reprojected_depth(vis_curr.prev_z(), vis_prev.z(), 1, vis_prev.dz_dxy())) continue;
			}

			if (c.a <= 0 || any(isnan(c))) continue;	

			const float wc = (xx == 0 ? (1 - w.x) : w.x) * (yy == 0 ? (1 - w.y) : w.y);
			color_prev   += c * wc;
			moments_prev += gPrevMoments[ipos_prev] * wc;
			sum_w        += wc;
		}

	float4 color_curr = gSamples[ipos];

	const float l = luminance(color_curr.rgb);
	const float2 moments_curr = float2(l, l*l);

	if (sum_w > 1e-4 && color_prev.a > 0) { // found sufficiently reliable history information
		const float invSum = 1/sum_w;
		color_prev   *= invSum;
		moments_prev *= invSum;

		float n = min(gPushConstants.gHistoryLimit, color_prev.a + color_curr.a);

		if (gAntilag) {
			float antilag_alpha = 0;

			if (gGradientFilterRadius == 0) {
				uint w,h;
				gSamples.GetDimensions(w,h);
				const float2 v = gDiff.SampleLevel(gSampler, (ipos + 0.5) / float2(w,h), 0);
				antilag_alpha = saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0);
			} else {
				for (int yy = -gGradientFilterRadius; yy <= gGradientFilterRadius; yy++)
					for (int xx = -gGradientFilterRadius; xx <= gGradientFilterRadius; xx++) {
						const int2 p = int2(ipos/gGradientDownsample) + int2(xx, yy);
						if (!test_inside_screen(p*gGradientDownsample, view)) continue;
						const float2 v = gDiff[p];
						antilag_alpha = max(antilag_alpha, saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0));
					}
			}
			antilag_alpha = saturate(antilag_alpha*gPushConstants.gAntilagScale);
			if (!isnan(antilag_alpha) && !isinf(antilag_alpha))		
				n = lerp(n, color_curr.a, antilag_alpha);
		}

		const float alpha = color_curr.a / n;

		gAccumColor[ipos] = float4(lerp(color_prev.rgb, color_curr.rgb, alpha), n);
		gAccumMoments[ipos] = lerp(moments_prev, moments_curr, max(0.6, alpha));
	} else {
		gAccumColor[ipos] = color_curr;
		gAccumMoments[ipos] = moments_curr;
	}
}