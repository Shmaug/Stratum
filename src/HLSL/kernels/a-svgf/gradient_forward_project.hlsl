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

#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -T cs_6_7 -E main

[[vk::constant_id(0)]] const uint gGradientDownsample = 3u;

#define PT_DESCRIPTOR_SET_0
#define PT_DESCRIPTOR_SET_1
#include "../pt_descriptors.hlsli"
#include "svgf_common.hlsli"

[[vk::push_constant]] const struct {
	uint gViewCount;
	uint gFrameNumber;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 extent_curr;
	gRadiance.GetDimensions(extent_curr.x,extent_curr.y);
	uint2 extent_prev;
	gPrevRadiance.GetDimensions(extent_prev.x,extent_prev.y);

	int2 idx_prev = index.xy*gGradientDownsample;
	const uint view_index = get_view_index(idx_prev, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;
	
	{
		const int2 idx_view_prev = idx_prev - gViews[view_index].image_min;
		uint2 arg = uint2(idx_prev.y*extent_prev.x + idx_prev.x, gPushConstants.gFrameNumber);
		uint sum = 0;
		for (uint i = 0; i < 32; i++) {
			sum += 0x9e3779b9;
			arg.x += ((arg.y << 4) + 0xa341316c) ^ (arg.y + sum) ^ ((arg.y >> 5) + 0xc8013ea4);
			arg.y += ((arg.x << 4) + 0xad90777d) ^ (arg.x + sum) ^ ((arg.x >> 5) + 0x7e95761e);
		}
		idx_prev += arg%gGradientDownsample;
	}
	if (!test_inside_screen(idx_prev, gViews[view_index])) return;

	const uint idx_prev_1d = idx_prev.y*extent_prev.x + idx_prev.x;
	const uint mapped_instance = gInstanceIndexMap[gPrevVisibility[idx_prev_1d].instance_index()];

	if (mapped_instance == INVALID_INSTANCE) return;

	// encode position in previous frame
	const int2 idx_view_prev = idx_prev - gViews[view_index].image_min;
	uint gradient_idx_curr = (idx_view_prev.x + idx_view_prev.y * gViews[view_index].extent().x) << (2 * TILE_OFFSET_SHIFT);

	const float3 position_curr = gInstances[mapped_instance].inv_motion_transform.transform_point(gPrevVisibility[idx_prev_1d].position);
	float4 screen_pos_curr = gViews[view_index].projection.project_point( gViews[view_index].world_to_camera.transform_point(position_curr) );
	screen_pos_curr.y = -screen_pos_curr.y;
	const float2 uv = (screen_pos_curr.xy/screen_pos_curr.w)*.5 + .5;
	const int2 idx_curr = gViews[view_index].image_min + int2(uv * gViews[view_index].extent());
	if (!test_inside_screen(idx_curr, gViews[view_index])) return;
	const uint idx_curr_1d = idx_curr.y*extent_curr.x + idx_curr.x;

	//if (gVisibility[idx_curr_1d].instance_index() != mapped_instance) return;
	//if (!test_reprojected_depth(gVisibility[idx_curr_1d].prev_z(), gPrevVisibility[idx_prev_1d].z(), gGradientDownsample, gPrevVisibility[idx_prev_1d].dz_dxy())) return;
	//if (!test_reprojected_normal(gVisibility[idx_curr_1d].normal(), gPrevVisibility[idx_prev_1d].normal())) return;

	const uint2 tile_pos_curr = idx_curr / gGradientDownsample;
	gradient_idx_curr |= get_gradient_idx_from_tile_pos(idx_curr % gGradientDownsample);

	uint res;
	InterlockedCompareExchange(gGradientSamples[tile_pos_curr], 0u, gradient_idx_curr, res);
	if (res == 0) {
		gPathStates[idx_curr_1d].rng_state = gPrevVisibility[idx_prev_1d].rng_state;
		const uint instance_primitive_index = mapped_instance | (gPrevVisibility[idx_prev_1d].primitive_index()<<16);
		make_shading_data(gPathStates[idx_curr_1d].vertex.shading_data, instance_primitive_index, gInstances[mapped_instance].inv_transform.transform_point(position_curr));

		gVisibility[idx_curr_1d].rng_state = gPrevVisibility[idx_prev_1d].rng_state;
		gVisibility[idx_curr_1d].position = position_curr;
		gVisibility[idx_curr_1d].instance_primitive_index =  instance_primitive_index;
		gVisibility[idx_curr_1d].nz = gPrevVisibility[idx_prev_1d].nz;
		gVisibility[idx_curr_1d].prev_uv = (idx_view_prev + 0.5) / gViews[view_index].extent();
		
		gReservoirs[idx_curr_1d] = gPrevReservoirs[idx_prev_1d];
	}
}