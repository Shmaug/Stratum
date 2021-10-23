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
#include "../rtscene.hlsli"

StructuredBuffer<VertexData> gVertices;
ByteAddressBuffer gIndices;
StructuredBuffer<InstanceData> gInstances;

RWTexture2D<uint4> gVisibility;
Texture2D<uint4> gPrevVisibility;
RWTexture2D<float4> gNormal;
Texture2D<float4> gPrevNormal;
RWTexture2D<float4> gZ;
Texture2D<float4> gPrevZ;
RWTexture2D<uint4> gRNGSeed;
Texture2D<uint4> gPrevRNGSeed;
RWTexture2D<float3> gPrevPos;
RWTexture2D<uint> gGradientSamples;

[[vk::push_constant]] const struct {
	TransformData gCameraToWorld;
	ProjectionData gProjection;
	uint2 gResolution;
	uint gFrameNumber;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	uint2 resolution;
	gVisibility.GetDimensions(resolution.x, resolution.y);

	uint2 idx_prev;
	{
		uint2 arg = uint2(index.x + index.y*resolution.x, gPushConstants.gFrameNumber);
		uint sum = 0;
		for (uint i = 0; i < 16; i++) { // XXX rounds reduced, carefully check if good
			//for(int i = 0; i < 32; i++) {
			sum += 0x9e3779b9;
			arg.x += ((arg.y << 4) + 0xa341316c) ^ (arg.y + sum) ^ ((arg.y >> 5) + 0xc8013ea4);
			arg.y += ((arg.x << 4) + 0xad90777d) ^ (arg.x + sum) ^ ((arg.x >> 5) + 0x7e95761e);
		}
		arg %= gGradientDownsample;
		idx_prev = index.xy*gGradientDownsample + arg;
	}

	if (any(idx_prev >= resolution)) return;

	uint res;

	uint4 vis = gPrevVisibility[idx_prev];
	float4 pos_cs_curr = project_point(gPushConstants.gProjection, transform_point(inverse(gPushConstants.gCameraToWorld), surface_attributes(gInstances[vis.x], gVertices, gIndices, vis.y, asfloat(vis.zw)).v.mPositionU.xyz));
	pos_cs_curr.y = -pos_cs_curr.y;
	int2 idx_curr = int2(((pos_cs_curr.xy/pos_cs_curr.w)*.5 + .5) * resolution);
	if (!test_inside_screen(idx_curr, resolution)) return;

	float4 z_curr = gZ[idx_curr];
	if (!test_reprojected_depth(z_curr.z, gPrevZ[idx_prev].x, z_curr.y)) return;
	if (!test_reprojected_normal(gNormal[idx_curr].xyz, gPrevNormal[idx_prev].xyz)) return;

	uint2 tile_pos_curr = idx_curr/gGradientDownsample;
	uint gradient_idx_curr = get_gradient_idx_from_tile_pos(idx_curr % gGradientDownsample);

	// encode position in previous frame
	gradient_idx_curr |= (idx_prev.x + idx_prev.y * resolution.x) << (2 * TILE_OFFSET_SHIFT);

	InterlockedCompareExchange(gGradientSamples[tile_pos_curr], 0u, gradient_idx_curr, res);
	if (res == 0) {
		gVisibility[idx_curr] = gPrevVisibility[idx_curr];
		gRNGSeed[idx_curr] = gPrevRNGSeed[idx_prev];
	}
}
