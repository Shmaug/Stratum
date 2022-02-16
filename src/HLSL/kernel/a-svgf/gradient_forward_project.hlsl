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

#include "svgf_shared.hlsli"
#include "../../scene.hlsli"

StructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gIndices;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<uint> gInstanceIndexMap;
StructuredBuffer<ViewData> gViews;
#include "../../visibility_buffer.hlsli"

RWTexture2D<uint> gGradientSamples;

[[vk::push_constant]] const struct {
	uint gViewCount;
	uint gFrameNumber;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	int2 idx_prev = index.xy*gGradientDownsample;
	const uint viewIndex = get_view_index(idx_prev, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	const ViewData view = gViews[viewIndex];
	const int2 view_size = view.image_max - view.image_min;

	{
		const int2 idx_view_prev = idx_prev - view.image_min;
		uint2 arg = uint2(idx_view_prev.x + idx_view_prev.y*view_size.x, gPushConstants.gFrameNumber);
		uint sum = 0;
		for (uint i = 0; i < 16; i++) { // XXX rounds reduced, carefully check if good
			//for(int i = 0; i < 32; i++) {
			sum += 0x9e3779b9;
			arg.x += ((arg.y << 4) + 0xa341316c) ^ (arg.y + sum) ^ ((arg.y >> 5) + 0xc8013ea4);
			arg.y += ((arg.x << 4) + 0xad90777d) ^ (arg.x + sum) ^ ((arg.x >> 5) + 0x7e95761e);
		}
		idx_prev += arg%gGradientDownsample;
	}
	if (!test_inside_screen(idx_prev, view)) return;

 	VisibilityInfo v_prev = load_prev_visibility(idx_prev, gInstanceIndexMap);
	if (v_prev.instance_index() == INVALID_INSTANCE) return;

	// encode position in previous frame
	const int2 idx_view_prev = idx_prev - view.image_min;
	uint gradient_idx_curr = (idx_view_prev.x + idx_view_prev.y * view_size.x) << (2 * TILE_OFFSET_SHIFT);

	const InstanceData instance = gInstances[v_prev.instance_index()];
	float3 pos_obj;
	if (v_prev.primitive_index() == INVALID_PRIMITIVE) {
		pos_obj = spherical_uv_to_cartesian(v_prev.bary());
	} else {
		const uint3 tri = load_tri(gIndices, instance, v_prev.primitive_index());
		const float3 p0 = gVertices[tri.x].position;
		pos_obj = p0 + (gVertices[tri.y].position - p0) * v_prev.bary().x + (gVertices[tri.z].position - p0) * v_prev.bary().y;
	}

	const float3 pos_cam_curr = tmul(view.world_to_camera, instance.transform).transform_point(pos_obj);
	float4 pos_cs_curr = view.projection.project_point(pos_cam_curr);
	pos_cs_curr.y = -pos_cs_curr.y;
	const float2 uv = (pos_cs_curr.xy/pos_cs_curr.w)*.5 + .5;
	const int2 idx_curr = view.image_min + int2(uv * view_size);
	if (!test_inside_screen(idx_curr, view)) return;

	const VisibilityInfo v_curr = load_visibility(idx_curr);

	if (!test_reprojected_depth(v_curr.prev_z(), v_prev.z(), 1, v_prev.dz_dx(), v_prev.dz_dy())) return;
	if (!test_reprojected_normal(v_curr.normal(), v_prev.normal())) return;

	const uint2 tile_pos_curr = idx_curr / gGradientDownsample;
	gradient_idx_curr |= get_gradient_idx_from_tile_pos(idx_curr % gGradientDownsample);

	uint res;
	InterlockedCompareExchange(gGradientSamples[tile_pos_curr], 0u, gradient_idx_curr, res);
	if (res == 0) {
		v_prev.data[2].zw = asuint( (idx_view_prev + 0.5) / float2(view_size) );
		for (uint i = 0; i < VISIBILITY_BUFFER_COUNT; i++)
			gVisibility[i][idx_curr] = v_prev.data[i];
			
		uint w,h;
		gVisibility[0].GetDimensions(w,h);
		const float3 dir = instance.transform.transform_point(pos_obj) - gPathStates[idx_curr.y*w + idx_curr.x].ray_origin;
		const float z = length(dir);
		RayDifferential ray = gPathStates[idx_curr.y*w + idx_curr.x].ray();
		ray.direction = dir/z;
		
		const float2 bary_or_z = v_prev.primitive_index() == INVALID_PRIMITIVE ? z : v_prev.bary();
		store_path_bounce_state(gPathStates[idx_curr.y*w + idx_curr.x], v_prev.rng_seed(), gPathStates[idx_curr.y*w + idx_curr.x].throughput(), gPathStates[idx_curr.y*w + idx_curr.x].eta_scale(), bary_or_z, ray, v_prev.data[1].x);
	}
}