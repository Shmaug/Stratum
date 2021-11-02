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

[[vk::constant_id(0)]] const float gHistoryLengthThreshold = 4;

[[vk::binding(0)]] RWTexture2D<float4> gOutput;
[[vk::binding(1)]] Texture2D<float4> gInput;
[[vk::binding(2)]] Texture2D<float2> gMoments;
[[vk::binding(3)]] Texture2D<float> gHistoryLength;
[[vk::binding(4)]] Texture2D<float4> gNormal;
[[vk::binding(5)]] Texture2D<float2> gZ;
[[vk::binding(6)]] Texture2D<uint4> gVisibility;

[[numthreads(8,8,1)]]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 resolution;
	gOutput.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float4 c = gInput[index.xy];
	float2 z_center = gZ[index.xy];
	if (isinf(z_center.x)) {
		gOutput[index.xy] = float4(c.rgb, 0);
		return;
	}

	float2 m = gMoments[index.xy].rg;
	float histlen = gHistoryLength[index.xy].r;
	if (histlen < gHistoryLengthThreshold) {
		{
			float l = luminance(c.rgb);
			m += float2(l, l*l);
		}

		float3 n_center = gNormal[index.xy].xyz;
		uint mesh_id_center = gVisibility[index.xy].x;

		float sum_w = 1;
		int r = histlen > 1 ? 2 : 3;
		for (int yy = -r; yy <= r; yy++)
			for (int xx = -r; xx <= r; xx++) {
				if (xx == 0 && yy == 0) continue;
				int2 p = int2(index.xy) + int2(xx, yy);
				if (any(p < 0) || any(index.xy >= resolution)) continue;
				if (mesh_id_center != gVisibility[p].x) continue;

				float z_p = gZ[p].x;
				float3 n_p = gNormal[p].xyz;

				float w_z = abs(z_p - z_center.x) / (z_center.y * length(float2(xx, yy)) + 1e-2);
				float w_n = pow(saturate(dot(n_p, n_center)), 128.0); 
				float w = exp(-w_z) * w_n;

				if (isnan(w) || isinf(w)) continue;

				float l = luminance(c.rgb);
				sum_w += w;
				m += float2(l, l*l) * w;
				c.rgb += gInput[p].rgb * w;
			}

		m /= sum_w;
		c.rgb /= sum_w;
		
		gOutput[index.xy] = float4(c.rgb, (1 + 3*(1 - histlen / gHistoryLengthThreshold)) * max(0, m.y - m.x*m.x));
	} else 
		gOutput[index.xy] = float4(c.rgb, max(0, m.y - m.x*m.x));
}
