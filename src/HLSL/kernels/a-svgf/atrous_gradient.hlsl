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

#define PT_DESCRIPTOR_SET_1
#include "../pt_descriptors.hlsli"
#include "svgf_common.hlsli"

[[vk::constant_id(0)]] const uint gGradientDownsample = 3u;
[[vk::constant_id(1)]] const uint gFilterKernelType = 0u;

[[vk::push_constant]] const struct {
	uint gViewCount;
	float gSigmaLuminanceBoost;
	uint gIteration;
	uint gStepSize;
} gPushConstants;

#define gInput1 gDiffImage1[gPushConstants.gIteration%2]
#define gInput2 gDiffImage2[gPushConstants.gIteration%2]
#define gOutput1 gDiffImage1[(gPushConstants.gIteration%2) ^ 1]
#define gOutput2 gDiffImage2[(gPushConstants.gIteration%2) ^ 1]

static const float gaussian_kernel[2][2] = {
	{ 1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 8.0, 1.0 / 16.0 }
};

class TapData {
	ViewData view;
	int2 ipos;
	float2 z_center;
	float l_center;
	float sigma_l;
	float2 sum_color;
	float sum_luminance;
	float sum_variance;
	float sum_weight;

	inline void compute_sigma_luminance() {
		const float kernel[2][2] = {
			{ 1.0 / 4.0, 1.0 / 8.0  },
			{ 1.0 / 8.0, 1.0 / 16.0 }
		};
		float sum = sum_variance * kernel[0][0];
		for (int yy = -1; yy <= 1; yy++)
			for (int xx = -1; xx <= 1; xx++) {
				if (xx == 0 && yy == 0) continue;
				const int2 p = ipos + int2(xx, yy);
				if (!test_inside_screen(p*gGradientDownsample, view)) continue;
				sum += gInput2[p].g * kernel[abs(xx)][abs(yy)];
			}
		sigma_l = sqrt(max(sum, 0))*gPushConstants.gSigmaLuminanceBoost;
	}

	void tap(int2 offset, float kernel_weight) {
		const int2 p = ipos + offset;
		if (!test_inside_screen(p*gGradientDownsample, view)) return;
		
		const float2 color1_p = gInput1[p]; 
		const float4 color2_p = gInput2[p]; 
		const float z_p       = color2_p.b;
		const float l_p       = color2_p.r;

		const float w_l = abs(l_p - l_center) / (sigma_l + 1e-8); 
		const float w_z = abs(z_p - z_center.x) / (z_center.y * length(int2(offset) * gPushConstants.gStepSize * gGradientDownsample) + 1e-2); 
		const float w = exp(-w_l * w_l - w_z) * kernel_weight;
		if (isinf(w) || isnan(w) || w == 0) return;

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

	if((gPushConstants.gIteration & 1) == 0) {
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
	for(int yy = -r; yy <= r; yy++)
		for(int xx = -r; xx <= r; xx++)
			if(xx != 0 || yy != 0)
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
void main(int2 index : SV_DispatchThreadId) {
	const uint viewIndex = get_view_index(index.xy*gGradientDownsample, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	TapData t;
	t.view = gViews[viewIndex];
	t.ipos = index.xy;
	const float2 color_center1 = gInput1[t.ipos];
	const float4 color_center2 = gInput2[t.ipos];
	t.sum_color     = color_center1;
	t.sum_luminance = color_center2.r;
	t.sum_variance  = color_center2.g;
	t.sum_weight    = 1;
	t.l_center      = color_center2.r;
	t.z_center      = color_center2.ba;

	t.compute_sigma_luminance();
	
	if (!isinf(t.z_center.x)) { // only filter foreground pixels
		switch (gFilterKernelType) {
		default:
		case FilterKernelType::eAtrous:
			atrous(t);
			break;
		case FilterKernelType::eBox3:
			box3(t);
			break;
		case FilterKernelType::eBox5:
			box5(t);
			break;
		case FilterKernelType::eSubsampled:
			subsampled(t);
			break;
		case FilterKernelType::eBox3Subsampled:
			if (gPushConstants.gStepSize == 1)
				box3(t);
			else
				subsampled(t);
			break;
		case FilterKernelType::eBox5Subsampled:
			if (gPushConstants.gStepSize == 1)
				box5(t);
			else
				subsampled(t);
			break;
		}
	}

	const float invSum = 1/t.sum_weight;
	t.sum_color     *= invSum;
	t.sum_luminance *= invSum;
	t.sum_variance  *= invSum*invSum;

	gOutput1[t.ipos] = t.sum_color;
	gOutput2[t.ipos] = float4(t.sum_luminance, t.sum_variance, t.z_center);
}
