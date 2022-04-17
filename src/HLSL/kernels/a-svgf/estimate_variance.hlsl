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

[[vk::constant_id(0)]] const bool gUseVisibility = true;

#define PT_DESCRIPTOR_SET_1
#include "../pt_descriptors.hlsli"
#include "svgf_common.hlsli"

[[vk::push_constant]] struct {
	uint gViewCount;
	float gHistoryLimit;
} gPushConstants;

#define GROUP_SIZE 8
#define RADIUS_0 2
#define RADIUS_1 3
#define TILE_SIZE (GROUP_SIZE + 2*RADIUS_1)

//groupshared uint3 s_mem[TILE_SIZE*TILE_SIZE];

inline min16float4 load(const uint2 index, const int2 tile_pos) {
	return (min16float4)gAccumColor[index];
	//return (min16float4)unpack_f16_4(s_mem[(RADIUS_1 + tile_pos.y)*TILE_SIZE + RADIUS_1 + tile_pos.x].xy);
}
inline min16float2 load_moment(const uint2 index, const int2 tile_pos) {
	return (min16float2)gAccumMoments[index];
	//return (min16float2)unpack_f16_2(s_mem[(RADIUS_1 + tile_pos.y)*TILE_SIZE + RADIUS_1 + tile_pos.x].z);
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void main(uint3 index : SV_DispatchThreadId, uint3 group_index : SV_GroupThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	/*
	if (all(group_index == 0)) {
		for (uint j = 0; j < TILE_SIZE; j++)
			for (uint i = 0; i < TILE_SIZE; i++) {
				const int2 p = int2(index.xy) + int2(i,j) - RADIUS_1;
				if (!test_inside_screen(p, gViews[view_index])) continue;
				s_mem[j*TILE_SIZE + i] = uint3(pack_f16_4(gAccumColor[p]), pack_f16_2(gAccumMoments[p]));
			}
	}
	GroupMemoryBarrierWithGroupSync();
	*/
	
	float4 c = load(index.xy, group_index.xy);
	float2 m = load_moment(index.xy, group_index.xy);

	uint2 extent;
	gFilterImages[0].GetDimensions(extent.x, extent.y);
	const uint index_1d = index.y*extent.x + index.x;

	const float histlen = c.a;
	if (gVisibility[index_1d].instance_index() == INVALID_INSTANCE || histlen >= gPushConstants.gHistoryLimit) {
		gFilterImages[0][index.xy] = float4(c.rgb, max(0, m.y - m.x*m.x));
		return;
	}

	float sum_w = 1;

	const int r = histlen > 1 ? RADIUS_0 : RADIUS_1;
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++) {
			if (xx == 0 && yy == 0) continue;

			const int2 p = int2(index.xy) + int2(xx, yy);
			if (!test_inside_screen(p, gViews[view_index])) continue;
			const uint p_1d = p.y*extent.x + p.x;
			if (gUseVisibility && gVisibility[index_1d].instance_index() != gVisibility[p_1d].instance_index()) continue;

			const float w_z = abs(gVisibility[p_1d].z() - gVisibility[index_1d].z()) / (length(gVisibility[index_1d].dz_dxy() * float2(xx, yy)) + 1e-2);
			const float w_n = pow(saturate(dot(gVisibility[p_1d].normal(), gVisibility[index_1d].normal())), 128); 
			const float w = gUseVisibility ? exp(-w_z) * w_n : 1;

			if (isnan(w) || isinf(w)) continue;
			
			m += load_moment(p, group_index.xy + int2(xx,yy)) * w;
			c.rgb += load(p, group_index.xy + int2(xx,yy)).rgb * w;
			sum_w += w;
		}
	
	sum_w = 1/sum_w;
	m *= sum_w;
	c.rgb *= sum_w;
	
	gFilterImages[0][index.xy] = float4(c.rgb, max(0, m.y - m.x*m.x)*(1 + 3*(1 - histlen/gPushConstants.gHistoryLimit)));
}