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
#pragma compile dxc -spirv -T cs_6_7 -E copy_rgb

#include "svgf_shared.hlsli"
#include "../../visibility_buffer.hlsli"

[[vk::constant_id(0)]] const uint gFilterKernelType = 1u;

RWTexture2D<float4> gImage[2];
StructuredBuffer<ViewData> gViews;

[[vk::push_constant]] const struct {
	uint gViewCount;
	float gSigmaLuminanceBoost;
	uint gIteration;
	uint gStepSize;
} gPushConstants;

#define gInput gImage[gPushConstants.gIteration%2]
#define gOutput gImage[(gPushConstants.gIteration+1)%2]

struct TapData {
	ViewData view;
	int2 index;
	uint2 s_mem_index;
	float3 center_normal;
	min16float z_center;
	min16float2 dz_center;
	float l_center;
	float sigma_l;

	float4 sum_color;
	float sum_weight;

	inline void compute_sigma_luminance() {
		const float kernel[2][2] = {
			{ 1.0 / 4.0, 1.0 / 8.0  },
			{ 1.0 / 8.0, 1.0 / 16.0 }
		};
		float s = sum_color.a*kernel[1][1];
		for (int yy = -1; yy <= 1; yy++)
			for (int xx = -1; xx <= 1; xx++) {
				if (xx == 0 && yy == 0) continue;
				const int2 p = index + int2(xx, yy);
				if (!test_inside_screen(p, view)) continue;
				s += gInput[p].a * kernel[abs(xx)][abs(yy)];
			}
		sigma_l = sqrt(max(s, 0))*gPushConstants.gSigmaLuminanceBoost;
	}

	inline void tap(const int2 offset, const float kernel_weight) {
		const int2 p = index + offset;
		if (!test_inside_screen(p, view)) return;

		const float4 color_p  = gInput[p];

		const float l_p = luminance(color_p.rgb);

		const VisibilityInfo v_p = load_visibility(p);

		const float w_l = abs(l_p - l_center) / max(sigma_l, 1e-10); 
		const float w_z = 3 * abs(v_p.z() - z_center) / (length(dz_center * min16float2(offset * gPushConstants.gStepSize)) + 1e-2);
		const float w_n = pow(max(0, dot(v_p.normal(), center_normal)), 256); 

		float w = exp(-pow2(w_l) - w_z) * kernel_weight * w_n;
		if (isinf(w) || isnan(w) || w == 0) return;

		sum_color  += color_p * float4(w, w, w, w*w); 
		sum_weight += w; 
	}
};


inline void subsampled(inout TapData t) {
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

inline void box3(inout TapData t) {
	const int r = 1;
	for (int yy = -r; yy <= r; yy++)
		for (int xx = -r; xx <= r; xx++)
			if (xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gPushConstants.gStepSize, 1.0);
}

inline void box5(inout TapData t) {
	const int r = 2;
	for(int yy = -r; yy <= r; yy++)
		for(int xx = -r; xx <= r; xx++)
			if(xx != 0 || yy != 0)
				t.tap(int2(xx, yy) * gPushConstants.gStepSize, 1.0);
}

inline void atrous(inout TapData t) {
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
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;

	TapData t;
	t.view = gViews[viewIndex];
	t.index = index.xy;
	const VisibilityInfo v = load_visibility(t.index);
	t.center_normal = v.normal();
	t.z_center = v.z();
	t.dz_center = v.dz_dxy();
	t.sum_weight = 1;
	t.sum_color = gInput[index.xy];
	t.l_center = luminance(t.sum_color.rgb);

	t.compute_sigma_luminance();

	if (!isinf(t.z_center)) { // only filter foreground pixels
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

	const float inv_w = 1/t.sum_weight;
	gOutput[t.index] = t.sum_color*float4(inv_w, inv_w, inv_w, inv_w*inv_w);
}

[numthreads(8,8,1)]
void copy_rgb(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gImage[0].GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;
	gImage[1][index.xy] = float4(gImage[0][index.xy].rgb, gImage[1][index.xy].w);
}
