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

[[vk::constant_id(0)]] const uint gFilterKernelType = 1u;
 
[[vk::binding(0)]] RWTexture2D<float4> gOutput;
[[vk::binding(1)]] Texture2D<float4> gInput;
[[vk::binding(2)]] Texture2D<float4> gAlbedo;
[[vk::binding(3)]] Texture2D<float4> gNormal;
[[vk::binding(4)]] Texture2D<float4> gZ;

[[vk::push_constant]] const struct {
	uint gIteration;
	uint gStepSize;
} gPushConstants;

static const float gaussian_kernel[3][3] = {
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
	{ 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
};

float compute_sigma_luminance(float center, int2 index) {
	const int r = 1;
	float sum = center * gaussian_kernel[0][0];
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++)
			if (xx != 0 || yy != 0) {
				float v = gInput[index + int2(xx, yy)].a;
				float w = gaussian_kernel[xx + 1][yy + 1];
				sum += v * w;
			}
	return sqrt(max(sum, 0.0));
}

class TapData {
	int2 index;
	uint2 resolution;
	float4 center_color;
	float3 center_normal;
	float2 z_center;
	float l_center;
	float sigma_l;

	float3 sum_color;
	float sum_variance;
	float sum_weight;

	void tap(int2 offset, float kernel_weight) {
		int2 p = index + offset;
		if (any(p < 0) || any(p >= resolution)) return;

		float4 color_p  = gInput[p];
		float3 normal_p = gNormal[p].xyz;
		float z_p       = gZ[p].x;
		float l_p       = luminance(color_p.rgb);

		float w_l = abs(l_p - l_center) / (sigma_l + 1e-10); 
		float w_z = 3.0 * abs(z_p - z_center.x) / (z_center.y * length(float2(offset) * gPushConstants.gStepSize) + 1e-2);
		float w_n = pow(max(0, dot(normal_p, center_normal)), 128.0); 

		float w = exp(-w_l * w_l - w_z) * kernel_weight * w_n;
		if (isinf(w) || isnan(w)) w = 0;

		sum_color    += color_p.rgb * w; 
		sum_variance += w * w * color_p.a; 
		sum_weight   += w; 
	}
};


void subsampled(inout TapData t) {
	/*
	| | |x| | |
	| |x| |x| |
	|x| |x| |x|
	| |x| |x| |
	| | |x| | |
	*/

	if ((gPushConstants.gIteration & 1) == 0) {
		/*
		| | | | | |
		| |x| |x| |
		|x| |x| |x|
		| |x| |x| |
		| | | | | |
		*/
		t.tap(int2(-2,  0) * gPushConstants.gStepSize, 1.0);
		t.tap(int2( 2,  0) * gPushConstants.gStepSize, 1.0);
	} else {
		/*
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		*/
		t.tap(int2( 0, -2) * gPushConstants.gStepSize, 1.0);
		t.tap(int2( 0,  2) * gPushConstants.gStepSize, 1.0);
	}

	t.tap(int2(-1,  1) * gPushConstants.gStepSize, 1.0);
	t.tap(int2( 1,  1) * gPushConstants.gStepSize, 1.0);

	t.tap(int2(-1, -1) * gPushConstants.gStepSize, 1.0);
	t.tap(int2( 1, -1) * gPushConstants.gStepSize, 1.0);
}

void box3(inout TapData t) {
	const int r = 1;
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++)
			if (xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gPushConstants.gStepSize, 1.0);
}

void box5(inout TapData t) {
	const int r = 2;
	for(int yy = -r; yy <= r; yy++)
		for(int xx = -r; xx <= r; xx++)
			if(xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gPushConstants.gStepSize, 1.0);
}

void atrous(inout TapData t) {
	const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

	t.tap(int2( 1,  0) * gPushConstants.gStepSize, 2.0 / 3.0);
	t.tap(int2( 0,  1) * gPushConstants.gStepSize, 2.0 / 3.0);
	t.tap(int2(-1,  0) * gPushConstants.gStepSize, 2.0 / 3.0);
	t.tap(int2( 0, -1) * gPushConstants.gStepSize, 2.0 / 3.0);

	t.tap(int2( 2,  0) * gPushConstants.gStepSize, 1.0 / 6.0);
	t.tap(int2( 0,  2) * gPushConstants.gStepSize, 1.0 / 6.0);
	t.tap(int2(-2,  0) * gPushConstants.gStepSize, 1.0 / 6.0);
	t.tap(int2( 0, -2) * gPushConstants.gStepSize, 1.0 / 6.0);

	t.tap(int2( 1,  1) * gPushConstants.gStepSize, 4.0 / 9.0);
	t.tap(int2(-1,  1) * gPushConstants.gStepSize, 4.0 / 9.0);
	t.tap(int2(-1, -1) * gPushConstants.gStepSize, 4.0 / 9.0);
	t.tap(int2( 1, -1) * gPushConstants.gStepSize, 4.0 / 9.0);

	t.tap(int2( 1,  2) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2(-1,  2) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2(-1, -2) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2( 1, -2) * gPushConstants.gStepSize, 1.0 / 9.0);

	t.tap(int2( 2,  1) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2(-2,  1) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2(-2, -1) * gPushConstants.gStepSize, 1.0 / 9.0);
	t.tap(int2( 2, -1) * gPushConstants.gStepSize, 1.0 / 9.0);

	t.tap(int2( 2,  2) * gPushConstants.gStepSize, 1.0 / 36.0);
	t.tap(int2(-2,  2) * gPushConstants.gStepSize, 1.0 / 36.0);
	t.tap(int2(-2, -2) * gPushConstants.gStepSize, 1.0 / 36.0);
	t.tap(int2( 2, -2) * gPushConstants.gStepSize, 1.0 / 36.0);
}

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadId) {
	TapData t;
	gOutput.GetDimensions(t.resolution.x, t.resolution.y);
	if (any(index.xy >= t.resolution)) return;
	t.index = index.xy;
	t.center_color = gInput[index.xy];
	t.center_normal = gNormal[index.xy].xyz;
	t.z_center = gZ[index.xy].xy;
	t.l_center = luminance(t.center_color.rgb);
	t.sigma_l  = compute_sigma_luminance(t.center_color.a, t.index) * 3.0;
	t.sum_color    = t.center_color.rgb;
	t.sum_variance = t.center_color.a;
	t.sum_weight   = 1.0;

	if (!isinf(t.z_center.x)) { // only filter foreground pixels
		if (gFilterKernelType == 0)
			atrous(t);
		else if (gFilterKernelType == 1)
			box3(t);
		else if (gFilterKernelType == 2)
			box5(t);
		else if (gFilterKernelType == 3)
			subsampled(t);
		else if (gFilterKernelType == 4) {
			if (gPushConstants.gStepSize == 1)
				box3(t);
			else
				subsampled(t);
		} else if (gFilterKernelType == 5) {
			if (gPushConstants.gStepSize == 1)
				box5(t);
			else
				subsampled(t);
		}
	}

	t.sum_color    /= t.sum_weight;
	t.sum_variance /= t.sum_weight * t.sum_weight;

	gOutput[index.xy] = float4(t.sum_color, t.sum_variance);
}
