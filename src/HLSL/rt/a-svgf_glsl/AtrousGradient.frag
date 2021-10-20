#version 440
#if 0

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

#endif

#include "colorspace.glsl"
#pragma optionNV(unroll all)

layout(location = 0) out vec4 frag_color1;
layout(location = 1) out vec4 frag_color2;

uniform PerImageCB {
	sampler2D tex_color1;
	sampler2D tex_color2;

	int iteration;
	int step_size;
	int gradientDownsample;
};

const float gaussian_kernel[3][3] = {
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
	{ 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
};

float
compute_sigma_luminance(float center, ivec2 ipos)
{
	const int r = 1;

	float sum = center * gaussian_kernel[0][0];

	for(int yy = -r; yy <= r; yy++) {
		for(int xx = -r; xx <= r; xx++) {
			if(xx != 0 || yy != 0) {
				ivec2 p = ipos + ivec2(xx, yy);
				float v = texelFetch(tex_color2, p, 0).g;
				float w = gaussian_kernel[xx + 1][yy + 1];
				sum += v * w;
			}
		}
	}

	return sqrt(max(sum, 0.0));
}


const ivec2 ipos = ivec2(gl_FragCoord);

const vec4  color_center1  = texelFetch(tex_color1,  ipos, 0);
const vec4  color_center2  = texelFetch(tex_color2,  ipos, 0);
const vec2  z_center      = color_center2.ba;

const float l_center      = color_center2.r;
const float sigma_l       = compute_sigma_luminance(color_center2.g, ipos) * 3.0;

vec4  sum_color       = color_center1;
float sum_luminance   = color_center2.r;
float sum_variance    = color_center2.g;
float sum_weight      = 1.0;



void
tap(ivec2 offset, float kernel_weight)
{
	ivec2 p = ipos + offset; 

	vec4  color1_p     = texelFetch(tex_color1,  p, 0); 
	vec4  color2_p     = texelFetch(tex_color2,  p, 0); 
	float z_p         = color2_p.b;

	float l_p         = color2_p.r;

	float w_l = abs(l_p - l_center) / (sigma_l + 1e-10); 
	float w_z = abs(z_p - z_center.x) / (z_center.y * length(vec2(offset) * step_size * gradientDownsample) + 1e-2); 

	float w = exp(-w_l * w_l - w_z) * kernel_weight; 

	sum_color     += color1_p * w; 
	sum_luminance += l_p * w;
	sum_variance  += w * w * color2_p.g; 
	sum_weight    += w; 
}

void
subsampled()
{
	/*
	| | |x| | |
	| |x| |x| |
	|x| |x| |x|
	| |x| |x| |
	| | |x| | |
	*/

	if((iteration & 1) == 0) {
		/*
		| | | | | |
		| |x| |x| |
		|x| |x| |x|
		| |x| |x| |
		| | | | | |
		*/
		tap(ivec2(-2,  0) * step_size, 1.0);
		tap(ivec2( 2,  0) * step_size, 1.0);
	}
	else {
		/*
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		| |x| |x| |
		| | |x| | |
		*/
		tap(ivec2( 0, -2) * step_size, 1.0);
		tap(ivec2( 0,  2) * step_size, 1.0);
	}

	tap(ivec2(-1,  1) * step_size, 1.0);
	tap(ivec2( 1,  1) * step_size, 1.0);

	tap(ivec2(-1, -1) * step_size, 1.0);
	tap(ivec2( 1, -1) * step_size, 1.0);
}

void
box3()
{
	const int r = 1;
	for(int yy = -r; yy <= r; yy++) {
		for(int xx = -r; xx <= r; xx++) {
			if(xx != 0 || yy != 0) {
				tap(ivec2(xx, yy) * step_size, 1.0);
			}
		}
	}
}

void
box5()
{
	const int r = 2;
	for(int yy = -r; yy <= r; yy++) {
		for(int xx = -r; xx <= r; xx++) {
			if(xx != 0 || yy != 0) {
				tap(ivec2(xx, yy) * step_size, 1.0);
			}
		}
	}
}

void
atrous()
{
	const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

	tap(ivec2( 1,  0) * step_size, 2.0 / 3.0);
	tap(ivec2( 0,  1) * step_size, 2.0 / 3.0);
	tap(ivec2(-1,  0) * step_size, 2.0 / 3.0);
	tap(ivec2( 0, -1) * step_size, 2.0 / 3.0);

	tap(ivec2( 2,  0) * step_size, 1.0 / 6.0);
	tap(ivec2( 0,  2) * step_size, 1.0 / 6.0);
	tap(ivec2(-2,  0) * step_size, 1.0 / 6.0);
	tap(ivec2( 0, -2) * step_size, 1.0 / 6.0);

	tap(ivec2( 1,  1) * step_size, 4.0 / 9.0);
	tap(ivec2(-1,  1) * step_size, 4.0 / 9.0);
	tap(ivec2(-1, -1) * step_size, 4.0 / 9.0);
	tap(ivec2( 1, -1) * step_size, 4.0 / 9.0);

	tap(ivec2( 1,  2) * step_size, 1.0 / 9.0);
	tap(ivec2(-1,  2) * step_size, 1.0 / 9.0);
	tap(ivec2(-1, -2) * step_size, 1.0 / 9.0);
	tap(ivec2( 1, -2) * step_size, 1.0 / 9.0);

	tap(ivec2( 2,  1) * step_size, 1.0 / 9.0);
	tap(ivec2(-2,  1) * step_size, 1.0 / 9.0);
	tap(ivec2(-2, -1) * step_size, 1.0 / 9.0);
	tap(ivec2( 2, -1) * step_size, 1.0 / 9.0);

	tap(ivec2( 2,  2) * step_size, 1.0 / 36.0);
	tap(ivec2(-2,  2) * step_size, 1.0 / 36.0);
	tap(ivec2(-2, -2) * step_size, 1.0 / 36.0);
	tap(ivec2( 2, -2) * step_size, 1.0 / 36.0);
}

void
main()
{
	if(z_center.x > 0) { /* only filter foreground pixels */
		box3();
	}
#if 0
	if(z_center.x > 0) { /* only filter foreground pixels */

#ifndef FILTER_KERNEL
#error filter kernel not defined
#endif

#if   FILTER_KERNEL == 0
		atrous();
#elif FILTER_KERNEL == 1
		box3();
#elif FILTER_KERNEL == 2
		box5();
#elif FILTER_KERNEL == 3
		subsampled();
#elif FILTER_KERNEL == 4
		if(step_size == 1)
			box3();
		else
			subsampled();
#elif FILTER_KERNEL == 5
		if(step_size == 1)
			box5();
		else
			subsampled();
#else
#error invalid filter kernel
#endif
	}
	#endif

	sum_color     /= sum_weight;
	sum_luminance /= sum_weight;
	sum_variance  /= sum_weight * sum_weight;


	frag_color1      = sum_color;
	frag_color2      = vec4(sum_luminance, sum_variance, z_center);
}
