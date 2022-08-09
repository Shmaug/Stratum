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

#if 0
#pragma compile dxc -spirv -T cs_6_7 -E main
#endif

#include "../denoiser.h"

struct PushConstants {
	uint gViewCount;
	float gHistoryLimit;
};

#ifdef __SLANG__
#ifndef gReprojection
#define gReprojection (false)
#endif
#ifndef gDebugMode
#define gDebugMode 0
#endif
[[vk::push_constant]] ConstantBuffer<PushConstants> gPushConstants;
#else // __SLANG___
[[vk::constant_id(0)]] const bool gReprojection = false;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::push_constant]] const PushConstants gPushConstants;
#endif

SLANG_SHADER("compute")
[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	float2 extent;
	gAccumColor.GetDimensions(extent.x, extent.y);

	const uint2 ipos = index.xy;

	float4 color_prev   = 0;
	float2 moments_prev = 0;
	float sum_w         = 0;
	if (gReprojection) {
		const VisibilityInfo vis = gVisibility[ipos.y*extent.x + ipos.x];
		if (vis.instance_index() != INVALID_INSTANCE) {
			const float2 pos_prev = gViews[view_index].image_min + gPrevUVs[ipos] * float2(gViews[view_index].image_max - gViews[view_index].image_min);
			const int2 p = pos_prev - 0.5;
			const float2 w = frac(pos_prev - 0.5);
			// bilinear interpolation, check each tap individually, renormalize afterwards
			for (int yy = 0; yy <= 1; yy++) {
				for (int xx = 0; xx <= 1; xx++) {
					const int2 ipos_prev = p + int2(xx, yy);
					if (!gViews[view_index].test_inside(ipos_prev)) continue;

					const VisibilityInfo prev_vis = gPrevVisibility[ipos_prev.y*extent.x + ipos_prev.x];
					if (gInstanceIndexMap[vis.instance_index()] != prev_vis.instance_index()) continue;
					if (dot(vis.normal(), prev_vis.normal()) < cos(degrees(5))) continue;
					if (abs(vis.prev_z() - prev_vis.z()) >= (length(prev_vis.dz_dxy())*1.25 + 1e-2)) continue;

					const float4 c = gPrevAccumColor[ipos_prev];

					if (c.a <= 0 || any(isnan(c)) || any(isinf(c)) || any(c != c)) continue;

					const float wc = (xx == 0 ? (1 - w.x) : w.x) * (yy == 0 ? (1 - w.y) : w.y);
					color_prev   += c * wc;
					moments_prev += gPrevAccumMoments[ipos_prev] * wc;
					sum_w        += wc;
				}
			}
		}
	} else {
		color_prev = gPrevAccumColor[ipos];
		if (any(isnan(color_prev.rgb)) || any(isinf(color_prev.rgb)))
			color_prev = 0;
		else
			moments_prev = gPrevAccumMoments[ipos];
		sum_w = 1;
	}

	float4 color_curr = gRadiance[ipos];
	if (any(isinf(color_curr.rgb)) || any(color_curr.rgb != color_curr.rgb)) color_curr = 0;
	if (any(isinf(moments_prev)) || any(moments_prev != moments_prev)) moments_prev = 0;

	if ((DenoiserDebugMode)gDebugMode == DenoiserDebugMode::eWeightSum)
		gDebugImage[ipos] = float4(viridis_quintic(sum_w), 1);

	const float l = luminance(color_curr.rgb);

	if (sum_w > 0 && color_prev.a > 0) {
		const float invSum = 1/sum_w;
		color_prev   *= invSum;
		moments_prev *= invSum;

		float n = color_prev.a + color_curr.a;
		if (gPushConstants.gHistoryLimit > 0 && n > gPushConstants.gHistoryLimit)
			n = gPushConstants.gHistoryLimit;

		if ((DenoiserDebugMode)gDebugMode == DenoiserDebugMode::eSampleCount) gDebugImage[ipos] = float4(viridis_quintic(saturate(n / (gPushConstants.gHistoryLimit ? gPushConstants.gHistoryLimit : 1024))), 1);

		const float alpha = saturate(color_curr.a / n);

		gAccumColor[ipos] = float4(lerp(color_prev.rgb, color_curr.rgb, alpha), n);
		gAccumMoments[ipos] = lerp(moments_prev, float2(l, l*l), max(0.6, alpha));
	} else {
		gAccumColor[ipos] = color_curr;
		gAccumMoments[ipos] = float2(l, l*l);
		if ((DenoiserDebugMode)gDebugMode == DenoiserDebugMode::eSampleCount) gDebugImage[ipos] = float4(viridis_quintic(0), 1);
	}
}