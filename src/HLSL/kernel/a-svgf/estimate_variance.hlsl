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

[[vk::constant_id(0)]] const float gHistoryLengthThreshold = 4;

RWTexture2D<float4> gOutput;
Texture2D<float4> gInput;
Texture2D<float2> gMoments;
StructuredBuffer<ViewData> gViews;
#include "../../visibility_buffer.hlsli"

[[vk::push_constant]] struct {
	uint gViewCount;
} gPushConstants;

#define GROUP_SIZE 8
#define RADIUS_0 2
#define RADIUS_1 3
#define TILE_SIZE (GROUP_SIZE + 2*RADIUS_1)
groupshared uint3 s_mem[TILE_SIZE*TILE_SIZE];

inline min16float4 load(const uint2 index, const int2 tile_pos) {
	//return gInput[index];
	return (min16float4)unpack_f16_4(s_mem[(RADIUS_1 + tile_pos.y)*TILE_SIZE + RADIUS_1 + tile_pos.x].xy);
}
inline min16float2 load_moment(const uint2 index, const int2 tile_pos) {
	//return gMoments[index];
	return (min16float2)unpack_f16_2(s_mem[(RADIUS_1 + tile_pos.y)*TILE_SIZE + RADIUS_1 + tile_pos.x].z);
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void main(uint3 index : SV_DispatchThreadId, uint3 group_index : SV_GroupThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	const ViewData view = gViews[viewIndex];

	if (all(group_index == 0)) {
		for (uint j = 0; j < TILE_SIZE; j++)
			for (uint i = 0; i < TILE_SIZE; i++) {
				const int2 p = int2(index.xy) + int2(i,j) - RADIUS_1;
				if (!test_inside_screen(p, view)) continue;
				s_mem[j*TILE_SIZE + i] = uint3(pack_f16_4(gInput[p]), pack_f16_2(gMoments[p]));
			}
	}
	GroupMemoryBarrierWithGroupSync();
	
	min16float4 c = load(index.xy, group_index.xy);
	min16float2 m = load_moment(index.xy, group_index.xy);

	const VisibilityInfo v = load_visibility(index.xy);

	const float histlen = c.a;
	if (v.instance_index() == INVALID_INSTANCE || histlen >= gHistoryLengthThreshold) {
		gOutput[index.xy] = float4(c.rgb, max(0, m.y - m.x*m.x));
		return;
	}

	min16float sum_w = 1;

	const int r = histlen > 1 ? RADIUS_0 : RADIUS_1;
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++) {
			if (xx == 0 && yy == 0) continue;

			const int2 p = int2(index.xy) + int2(xx, yy);
			if (!test_inside_screen(p, view)) continue;
			const VisibilityInfo v_p = load_visibility(p);
			if (v.instance_index() != v_p.instance_index()) continue;

			const float w_z = abs(v_p.z() - v.z()) / (length(v.dz_dxy() * float2(xx, yy)) + 1e-2);
			const float w_n = pow(saturate(dot(v_p.normal(), v.normal())), 128); 
			const min16float w = (min16float)(exp(-w_z) * w_n);

			if (isnan(w) || isinf(w)) continue;
			
			m += load_moment(p, group_index.xy + int2(xx,yy)) * w;
			c.rgb += load(p, group_index.xy + int2(xx,yy)).rgb * w;
			sum_w += w;
		}
	
	sum_w = 1/sum_w;
	m *= sum_w;
	c.rgb *= sum_w;
	
	gOutput[index.xy] = float4(c.rgb, max(0, m.y - m.x*m.x)*(1 + 3*(1 - histlen/gHistoryLengthThreshold)));
}