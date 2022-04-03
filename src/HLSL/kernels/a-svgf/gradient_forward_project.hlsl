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

#include "svgf_shared.hlsli"
#include "../../scene.hlsli"
#include "../../reservoir.hlsli"

StructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gIndices;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<uint> gInstanceIndexMap;

StructuredBuffer<ViewData> gViews;
#include "../../visibility_buffer.hlsli"
#include "../../path_state.hlsli"
RWStructuredBuffer<PathState> gPathStates;
RWStructuredBuffer<PathVertexGeometry> gPathVertices;
RWStructuredBuffer<Reservoir> gReservoirs;
StructuredBuffer<Reservoir> gPrevReservoirs;

RWTexture2D<uint> gGradientSamples;

[[vk::push_constant]] const struct {
	uint gViewCount;
	uint gFrameNumber;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	int2 idx_prev = index.xy*gGradientDownsample;
	const uint view_index = get_view_index(idx_prev, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	{
		const int2 idx_view_prev = idx_prev - gViews[view_index].image_min;
		uint2 arg = uint2(idx_view_prev.x + idx_view_prev.y*gViews[view_index].extent().x, gPushConstants.gFrameNumber);
		uint sum = 0;
		for (uint i = 0; i < 16; i++) { // XXX rounds reduced, carefully check if good
			//for(int i = 0; i < 32; i++) {
			sum += 0x9e3779b9;
			arg.x += ((arg.y << 4) + 0xa341316c) ^ (arg.y + sum) ^ ((arg.y >> 5) + 0xc8013ea4);
			arg.y += ((arg.x << 4) + 0xad90777d) ^ (arg.x + sum) ^ ((arg.x >> 5) + 0x7e95761e);
		}
		idx_prev += arg%gGradientDownsample;
	}
	if (!test_inside_screen(idx_prev, gViews[view_index])) return;

 	const VisibilityInfo v_prev = load_prev_visibility(idx_prev, gInstanceIndexMap);
	if (v_prev.instance_index() == INVALID_INSTANCE) return;

	// encode position in previous frame
	const int2 idx_view_prev = idx_prev - gViews[view_index].image_min;
	uint gradient_idx_curr = (idx_view_prev.x + idx_view_prev.y * gViews[view_index].extent().x) << (2 * TILE_OFFSET_SHIFT);

	float3 pos_obj;
	if (gInstances[v_prev.instance_index()].type() == INSTANCE_TYPE_SPHERE) {
		// sphere
		pos_obj = spherical_uv_to_cartesian(v_prev.bary())*gInstances[v_prev.instance_index()].radius();
	} else {
		// triangles
		const uint3 tri = load_tri(gIndices, gInstances[v_prev.instance_index()], v_prev.primitive_index());
		const float3 p0 = gVertices[tri.x].position;
		pos_obj = p0 + (gVertices[tri.y].position - p0) * v_prev.bary().x + (gVertices[tri.z].position - p0) * v_prev.bary().y;
	}

	float4 pos_cs_curr = gViews[view_index].projection.project_point( tmul(gViews[view_index].world_to_camera, gInstances[v_prev.instance_index()].transform).transform_point(pos_obj) );
	pos_cs_curr.y = -pos_cs_curr.y;
	const float2 uv = (pos_cs_curr.xy/pos_cs_curr.w)*.5 + .5;
	const int2 idx_curr = gViews[view_index].image_min + int2(uv * gViews[view_index].extent());
	if (!test_inside_screen(idx_curr, gViews[view_index])) return;

	const VisibilityInfo v_curr = load_visibility(idx_curr);
	if (v_curr.instance_index() != v_prev.instance_index()) return;
	if (!test_reprojected_depth(v_curr.prev_z(), v_prev.z(), gGradientDownsample, v_prev.dz_dxy())) return;
	if (!test_reprojected_normal(v_curr.normal(), v_prev.normal())) return;

	const uint2 tile_pos_curr = idx_curr / gGradientDownsample;
	gradient_idx_curr |= get_gradient_idx_from_tile_pos(idx_curr % gGradientDownsample);

	uint res;
	InterlockedCompareExchange(gGradientSamples[tile_pos_curr], 0u, gradient_idx_curr, res);
	if (res == 0) {
		gVisibility[0][idx_curr] = v_prev.data[0];
		gVisibility[1][idx_curr] = v_prev.data[1];
		gVisibility[2][idx_curr] = uint4(v_prev.data[2].xy, asuint( (idx_view_prev + 0.5) / gViews[view_index].extent() ));
		
		uint w,h;
		gVisibility[0].GetDimensions(w,h);
		const uint index_1d = idx_curr.y*w + idx_curr.x;

		gPathStates[index_1d].rng_state = v_prev.data[0];
		gPathStates[index_1d].instance_primitive_index = v_prev.instance_index() | (v_prev.primitive_index()<<16);
		if (gInstances[v_prev.instance_index()].type() != INSTANCE_TYPE_SPHERE)
			pos_obj = gInstances[v_prev.instance_index()].transform.transform_point(pos_obj);
		instance_geometry(gPathVertices[index_1d], gPathStates[index_1d].instance_primitive_index, pos_obj, v_prev.bary());
		
		gReservoirs[index_1d] = gPrevReservoirs[index_1d];
	}
}