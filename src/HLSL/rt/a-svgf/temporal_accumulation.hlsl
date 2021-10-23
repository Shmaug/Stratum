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

[[vk::constant_id(0)]] const bool gAntilag = false;
[[vk::constant_id(1)]] const uint gGradientFilterRadius = 2;

[[vk::binding(0)]] RWTexture2D<float4> gAccumColor;
[[vk::binding(1)]] RWTexture2D<float2> gAccumMoments;
[[vk::binding(2)]] RWTexture2D<float> gAccumLength;

[[vk::binding(3)]] Texture2D<float4> gColor;
[[vk::binding(4)]] Texture2D<float4> gPrevColor;
[[vk::binding(5)]] Texture2D<uint4> gVisibility;
[[vk::binding(6)]] Texture2D<float2> gPrevUV;
[[vk::binding(7)]] Texture2D<float4> gZ;
[[vk::binding(8)]] Texture2D<float4> gPrevZ;
[[vk::binding(9)]] Texture2D<float2> gPrevMoments;
[[vk::binding(10)]] Texture2D<float> gHistoryLength;
[[vk::binding(11)]] Texture2D<float4> gNormal;
[[vk::binding(12)]] Texture2D<float4> gPrevNormal;
[[vk::binding(13)]] Texture2D<float4> gDiff;
[[vk::binding(14)]] SamplerState gSampler;

[[vk::push_constant]] cbuffer {
	float2 gJitterOffset;
	float gTemporalAlpha;
};

[[numthreads(8,8,1)]]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 resolution;
  gZ.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

  uint2 ipos = index.xy;
	float2 pos_prev = gPrevUV[ipos] * float2(resolution) + gJitterOffset;

	uint2 p = pos_prev - 0.5;
	float2 w = (pos_prev - 0.5) - floor(pos_prev - 0.5);

	float4 z_curr      = gZ[ipos];
	float3 color_curr  = gColor[ipos].rgb;
	float3 normal_curr = gNormal[ipos].rgb;
	float l = luminance(color_curr);
	float2 moments_curr = float2(l, l*l);
	uint mesh_id_curr = gVisibility[ipos].x;

	float4 color_prev   = 0;
	float2 moments_prev = 0;
	float sum_w         = 0;
	float histlen       = 0;
	// bilinear interpolation, check each tap individually, renormalize afterwards
	for (int yy = 0; yy <= 1; yy++) {
		for (int xx = 0; xx <= 1; xx++) {
			int2 ipos_prev = p + int2(xx, yy);
			if (!test_inside_screen(ipos_prev, resolution)) continue;
			if (!test_reprojected_depth(z_curr.z, gPrevZ[ipos_prev].x, z_curr.y)) continue;
			if (!test_reprojected_normal(normal_curr, gPrevNormal[ipos_prev].xyz)) continue;
			if (gVisibility[ipos_prev].x != mesh_id_curr) continue;

			float4 c = gPrevColor[ipos_prev];
			if (any(isnan(c))) continue;

			float w = (xx == 0 ? (1.0 - w.x) : w.x) * (yy == 0 ? (1.0 - w.y) : w.y);
			color_prev   += c * w;
			moments_prev += gPrevMoments[ipos_prev] * w;
			histlen      += gHistoryLength[ipos_prev] * w;
			sum_w        += w;
		}
	}

  float antilag_alpha = 0;
  if (gAntilag) {
    const float gaussian_kernel[3][3] = {
      { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
      { 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
      { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
    };

    float4 v = gDiff.SampleLevel(gSampler, (ipos + 0.5)/float2(resolution), 0);

    for(int yy = -gGradientFilterRadius; yy <= gGradientFilterRadius; yy++) {
      for(int xx = -gGradientFilterRadius; xx <= gGradientFilterRadius; xx++) {
        float4 v = gDiff[ipos/gGradientDownsample + int2(xx, yy)];
        float a = clamp(abs(v.r > 1e-4 ? abs(v.g) / v.r : 0.0), 0.0, 200.0);
        float w = 1.0 / float((2 * gGradientFilterRadius + 1) * (2 * gGradientFilterRadius + 1));
        antilag_alpha = max(antilag_alpha, a);
      }
    }
    antilag_alpha = saturate(antilag_alpha);
    if (isnan(antilag_alpha))
      antilag_alpha = 1;
  }

	if (sum_w > 0.01) { /* found sufficiently reliable history information */
		color_prev   /= sum_w;
		moments_prev /= sum_w;
		histlen      /= sum_w;

    float alpha_color, alpha_moments;
    if (gAntilag) {
      alpha_color   = max(gTemporalAlpha, 1.0 / (histlen + 1.0));
      alpha_moments = max(0.6, 1.0 / (histlen + 1.0));

      alpha_color   = lerp(alpha_color,   1.0, antilag_alpha);
      alpha_moments = lerp(alpha_moments, 1.0, antilag_alpha);
    } else {
      alpha_color   = max(gTemporalAlpha, 1.0 / (histlen + 1.0));
      alpha_moments = max(0.6, 1.0 / (histlen + 1.0));
    }
		
		gAccumColor[ipos] = float4(lerp(color_prev.rgb, color_curr, alpha_color), min(64, histlen + 1.0));
		gAccumMoments[ipos] = lerp(moments_prev, moments_curr, alpha_moments);
		gAccumLength[ipos] = gAntilag ? clamp(1.0 / alpha_color, 0.0, 64.0) : min(64, histlen + 1.0);
	} else {
		gAccumColor[ipos] = float4(color_curr, 1.0);
		gAccumMoments[ipos] = moments_curr;
		gAccumLength[ipos] = 1;
	}

}