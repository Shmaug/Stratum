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

[[vk::constant_id(0)]] const uint gFilterKernelType = 1;

[[vk::binding(0)]] RWTexture2D<float> gOutput1;
[[vk::binding(1)]] RWTexture2D<float> gOutput2;
[[vk::binding(2)]] Texture2D<float4> gInput1;
[[vk::binding(2)]] Texture2D<float4> gInput2;

[[vk::push_constant]] cbuffer {
	int gIteration;
	uint gStepSize;
};

const float gaussian_kernel[3][3] = {
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
	{ 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
};

float compute_sigma_luminance(float center, int2 ipos) {
	const int r = 1;
	float sum = center * gaussian_kernel[0][0];
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++)
			if (xx != 0 || yy != 0)
				sum += gInput2[int3(ipos + int2(xx, yy), 0)].g * gaussian_kernel[xx + 1][yy + 1];
	return sqrt(max(sum, 0.0));
}

class TapData {
	int2 ipos;
	float4 color_center1;
	float4 color_center2;
	float2 z_center;
	float l_center;
	float sigma_l;
	float4 sum_color;
	float sum_luminance;
	float sum_variance;
	float sum_weight;
	uint2 resolution;

	void tap(int2 offset, float kernel_weight) {
		int2 p = ipos + offset; 
		if (any(p < 0) || any(p >= resolution)) return;
		
		float4 color1_p = gInput1[int3(p,0)]; 
		float4 color2_p = gInput2[int3(p,0)]; 
		float z_p       = color2_p.b;
		float l_p       = color2_p.r;

		float w_l = abs(l_p - l_center) / (sigma_l + 1e-10); 
		float w_z = abs(z_p - z_center.x) / (z_center.y * length(int2(offset) * gStepSize * gGradientDownsample) + 1e-2); 
		float w = exp(-w_l * w_l - w_z) * kernel_weight;

		if (isinf(w) || isnan(w)) w = 0;

		sum_color     += color1_p * w; 
		sum_luminance += l_p * w;
		sum_variance  += w * w * color2_p.g; 
		sum_weight    += w; 
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

	if((gIteration & 1) == 0) {
		/*
		| | | | | |
		| |x| |x| |
		|x| |x| |x|
		| |x| |x| |
		| | | | | |
		*/
		t.tap(int2(-2,  0) * gStepSize, 1.0);
		t.tap(int2( 2,  0) * gStepSize, 1.0);
	} else {
		/*
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		*/
		t.tap(int2( 0, -2) * gStepSize, 1.0);
		t.tap(int2( 0,  2) * gStepSize, 1.0);
	}

	t.tap(int2(-1,  1) * gStepSize, 1.0);
	t.tap(int2( 1,  1) * gStepSize, 1.0);

	t.tap(int2(-1, -1) * gStepSize, 1.0);
	t.tap(int2( 1, -1) * gStepSize, 1.0);
}

void box3(inout TapData t) {
	const int r = 1;
	for(int yy = -r; yy <= r; yy++)
		for(int xx = -r; xx <= r; xx++)
			if(xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gStepSize, 1.0);
}

void box5(inout TapData t) {
	const int r = 2;
	for(int yy = -r; yy <= r; yy++)
		for(int xx = -r; xx <= r; xx++)
			if(xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gStepSize, 1.0);
}

void atrous(inout TapData t) {
	const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

	t.tap(int2( 1,  0) * gStepSize, 2.0 / 3.0);
	t.tap(int2( 0,  1) * gStepSize, 2.0 / 3.0);
	t.tap(int2(-1,  0) * gStepSize, 2.0 / 3.0);
	t.tap(int2( 0, -1) * gStepSize, 2.0 / 3.0);

	t.tap(int2( 2,  0) * gStepSize, 1.0 / 6.0);
	t.tap(int2( 0,  2) * gStepSize, 1.0 / 6.0);
	t.tap(int2(-2,  0) * gStepSize, 1.0 / 6.0);
	t.tap(int2( 0, -2) * gStepSize, 1.0 / 6.0);

	t.tap(int2( 1,  1) * gStepSize, 4.0 / 9.0);
	t.tap(int2(-1,  1) * gStepSize, 4.0 / 9.0);
	t.tap(int2(-1, -1) * gStepSize, 4.0 / 9.0);
	t.tap(int2( 1, -1) * gStepSize, 4.0 / 9.0);

	t.tap(int2( 1,  2) * gStepSize, 1.0 / 9.0);
	t.tap(int2(-1,  2) * gStepSize, 1.0 / 9.0);
	t.tap(int2(-1, -2) * gStepSize, 1.0 / 9.0);
	t.tap(int2( 1, -2) * gStepSize, 1.0 / 9.0);

	t.tap(int2( 2,  1) * gStepSize, 1.0 / 9.0);
	t.tap(int2(-2,  1) * gStepSize, 1.0 / 9.0);
	t.tap(int2(-2, -1) * gStepSize, 1.0 / 9.0);
	t.tap(int2( 2, -1) * gStepSize, 1.0 / 9.0);

	t.tap(int2( 2,  2) * gStepSize, 1.0 / 36.0);
	t.tap(int2(-2,  2) * gStepSize, 1.0 / 36.0);
	t.tap(int2(-2, -2) * gStepSize, 1.0 / 36.0);
	t.tap(int2( 2, -2) * gStepSize, 1.0 / 36.0);
}

[[numthreads(8,8,1)]]
void main(int2 index : SV_DispatchThreadId) {
	TapData t;
	gOutput1.GetDimensions(t.resolution.x, t.resolution.y);
	if (any(index.xy >= t.resolution)) return;
	t.ipos = index.xy;
	t.color_center1 = gInput1[index];
	t.color_center2 = gInput2[index];
	t.z_center      = t.color_center2.ba;
	t.l_center      = t.color_center2.r;
	t.sigma_l       = compute_sigma_luminance(t.color_center2.g, t.ipos) * 3.0;
	t.sum_color     = t.color_center1;
	t.sum_luminance = t.color_center2.r;
	t.sum_variance  = t.color_center2.g;
	t.sum_weight    = 1;
	
	if (!isinf(t.z_center.x)) { /* only filter foreground pixels */
		if (gFilterKernelType == 0)
			atrous(t);
		else if (gFilterKernelType == 1)
			box3(t);
		else if (gFilterKernelType == 2)
			box5(t);
		else if (gFilterKernelType == 3)
			subsampled(t);
		else if (gFilterKernelType == 4){
			if (gStepSize == 1)
				box3(t);
			else
				subsampled(t);
		} else if (gFilterKernelType == 5) {
			if (gStepSize == 1)
				box5(t);
			else
				subsampled(t);
		}
	}

	t.sum_color     /= t.sum_weight;
	t.sum_luminance /= t.sum_weight;
	t.sum_variance  /= t.sum_weight * t.sum_weight;

	gOutput1[index] = t.sum_color;
	gOutput2[index] = float4(t.sum_luminance, t.sum_variance, t.z_center);
}