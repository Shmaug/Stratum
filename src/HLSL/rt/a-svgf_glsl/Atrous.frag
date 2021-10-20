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

layout(location = 0) out vec4 frag_color;

uniform PerImageCB {
	sampler2D tex_color;
	sampler2D tex_z;
	sampler2D tex_normal;
	sampler2D tex_albedo;

	int iteration;
	int step_size;
	int modulate_albedo;
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
				float v = texelFetch(tex_color, p, 0).a;
				float w = gaussian_kernel[xx + 1][yy + 1];
				sum += v * w;
			}
		}
	}

	return sqrt(max(sum, 0.0));
}


const ivec2 ipos = ivec2(gl_FragCoord);

const vec4  color_center  = texelFetch(tex_color,  ipos, 0);
const vec3  normal_center = texelFetch(tex_normal, ipos, 0).rgb;
const vec4  z_center      = texelFetch(tex_z,      ipos, 0);

const float l_center      = luminance(color_center.rgb);
const float sigma_l       = compute_sigma_luminance(color_center.a, ipos) * 3.0;

vec3  sum_color       = color_center.rgb;
float sum_variance    = color_center.a;
float sum_weight      = 1.0;



void
tap(ivec2 offset, float kernel_weight)
{
	ivec2 p = ipos + offset; 

	vec4  color_p     = texelFetch(tex_color,  p, 0); 
	vec3  normal_p    = texelFetch(tex_normal, p, 0).rgb; 
	float z_p         = texelFetch(tex_z,      p, 0).x;
	float l_p         = luminance(color_p.rgb); 

	float w_l = abs(l_p - l_center) / (sigma_l + 1e-10); 
	float w_z = 3.0 * abs(z_p - z_center.x) / (z_center.y * length(vec2(offset) * step_size) + 1e-2); 
	float w_n = pow(max(0, dot(normal_p, normal_center)), 128.0); 

	float w = exp(-w_l * w_l - w_z) * kernel_weight * w_n; 

	sum_color    += color_p.rgb * w; 
	sum_variance += w * w * color_p.a; 
	sum_weight   += w; 
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

	sum_color    /= sum_weight;
	sum_variance /= sum_weight * sum_weight;

	frag_color      = vec4(sum_color, sum_variance);

	if(modulate_albedo > 0) {
		frag_color.rgb *= texelFetch(tex_albedo, ipos, 0).rgb;
	}
}
