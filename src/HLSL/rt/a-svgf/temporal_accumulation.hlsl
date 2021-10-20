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
[[vk::constant_id(1)]] const bool gShowAntilagAlpha = false;
[[vk::constant_id(2)]] const uint gGradientFilterRadius = 2;

RWTexture2D<float4> gAccumulated;
RWTexture2D<float2> gMoments;
RWTexture2D<float> gHistoryLengthOut;
RWTexture2D<float> gAntilagAlpha;
Texture2D<float4> gColor;
Texture2D<float4> gPrevColor;
Texture2D<float2> gPrevUV;
Texture2D<float2> gZ;
Texture2D<float2> gPrevZ;
Texture2D<float2> gPrevMoments;
Texture2D<float> gHistoryLengthIn;
Texture2D<float4> gNormalId;
Texture2D<float4> gPrevNormalId;
Texture2D<uint> gGradientSamples;

Texture2D<float4> gDiff;

[[vk::push_constant]] cbuffer {
	float2 gJitterOffset;
	float gTemporalAlpha;
};

[[numthreads(8,8,1)]]
void main(uint3 index : SV_DispatchThreadId) {
  uint2 ipos = index.xy;
	uint2 size;
  gZ.GetDimensions(size.x, size.y);

	float2 pos_prev = gPrevUV[ipos] * size;

	uint2 p = pos_prev - 0.5;
	float2 w = (pos_prev - 0.5) - floor(pos_prev - 0.5);

	float2 z_curr       = gZ[ipos];
	float3 color_curr   = gColor[ipos].rgb;
	float3 normal_curr  = gNormalId[ipos].rgb;
	float l = luminance(color_curr);
	float2 moments_curr = float2(l, l*l);
	uint mesh_id_curr = asuint(gNormalId[ipos].w);

	float4 color_prev   = 0;
	float2 moments_prev = 0;
	float sum_w       = 0;
	float histlen     = 0;
	// bilinear interpolation, check each tap individually, renormalize afterwards
	for(int yy = 0; yy <= 1; yy++) {
		for(int xx = 0; xx <= 1; xx++) {
			int2 ipos_prev     = p + int2(xx, yy);
			float z_prev       = gPrevZ[ipos_prev].x;
			float3 normal_prev = gPrevNormalId[ipos_prev].xyz;
			uint mesh_id_prev  = asuint(gPrevNormalId[ipos_prev].w);

			bool accept = true;
			accept = accept && test_inside_screen(ipos_prev, size);
			accept = accept && test_reprojected_normal(normal_curr, normal_prev);
			accept = accept && test_reprojected_depth(z_curr.x, z_prev, z_curr.y);
			accept = accept && mesh_id_prev == mesh_id_curr;

			if(accept) {
				float w = (xx == 0 ? (1.0 - w.x) : w.x)
					      * (yy == 0 ? (1.0 - w.y) : w.y);
				color_prev   += gPrevColor[ipos_prev] * w;
				moments_prev += gPrevMoments[ipos_prev] * w;
				histlen      += gHistoryLengthIn[ipos_prev] * w;
				sum_w        += w;

			}
		}
	}

  float antilag_alpha = 0;
  if (gAntilag) {
    const float gaussian_kernel[3][3] = {
      { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
      { 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
      { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
    };

    float4 v = gDiff[ipos];
    for(int yy = -gGradientFilterRadius; yy <= gGradientFilterRadius; yy++) {
      for(int xx = -gGradientFilterRadius; xx <= gGradientFilterRadius; xx++) {
        float4 v = gDiff[ipos + int2(xx, yy)];
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

      if (gShowAntilagAlpha)
        gAntilagAlpha[ipos] = alpha_color;
    } else {
      alpha_color   = max(gTemporalAlpha, 1.0 / (histlen + 1.0));
      alpha_moments = max(0.6, 1.0 / (histlen + 1.0));
    }
		
		gAccumulated[ipos] = float4(lerp(color_prev.rgb, color_curr, alpha_color), min(64, histlen + 1.0));
		gMoments[ipos]     = lerp(moments_prev, moments_curr, alpha_moments);
		if (gAntilag)
	  	gHistoryLengthOut[ipos] = clamp(1.0 / alpha_color, 0.0, 64.0);
		else
  		gHistoryLengthOut[ipos] = min(64, histlen + 1.0);
	} else {
		gAccumulated[ipos] = float4(color_curr, 1.0);
		gMoments[ipos]     = moments_curr;
		gHistoryLengthOut[ipos] = 1;
    if (gAntilag && gShowAntilagAlpha)
  		gAntilagAlpha[ipos] = 1;
	}

}
