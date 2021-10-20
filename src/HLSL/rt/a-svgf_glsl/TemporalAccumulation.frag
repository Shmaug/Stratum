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

#include "svgf_shared.glsl"
#include "colorspace.glsl"
#include "viridis.glsl"

#pragma optionNV(unroll all)

uniform PerImageCB {
	sampler2D tex_color;
	sampler2D tex_color_prev;
	sampler2D tex_motion;
	sampler2D tex_z_curr;
	sampler2D tex_z_prev;
	sampler2D tex_moments_prev;
	sampler2D tex_history_length;
	sampler2D tex_normal_curr;
	sampler2D tex_normal_prev;
	sampler2D tex_vbuf_curr;
	sampler2D tex_vbuf_prev;

	#ifdef ANTILAG
	sampler2D tex_diff_current;

	layout(rgba8) image2D img_antilag_alpha;
	int gradientDownsample;
	usampler2D tex_gradient_samples;

	int gradientFilterRadius;
	#endif

	vec2 jitter_offset;
	float temporal_alpha;
};

layout(location = 0) out vec4 out_accumulated;
layout(location = 1) out vec2 out_moments;
layout(location = 2) out float out_histlen;

in  vec2 texC;


void
main()
{
	ivec2 ipos   = ivec2(gl_FragCoord);
	ivec2 size   = textureSize(tex_motion, 0).rg;

	vec2 motion   = texelFetch(tex_motion, ipos, 0).rg + jitter_offset;
	vec2 pos_prev = gl_FragCoord.xy + motion * 0.5 * vec2(textureSize(tex_z_prev, 0));

	ivec2 p = ivec2(pos_prev - 0.5);
	vec2  w = (pos_prev - 0.5) - floor(pos_prev - 0.5);

	vec4  z_curr       = texelFetch(tex_z_curr,      ipos, 0);
	vec3  color_curr   = texelFetch(tex_color,       ipos, 0).rgb;
	vec3  normal_curr  = texelFetch(tex_normal_curr, ipos, 0).rgb;
	float l            = luminance(color_curr);
	vec2  moments_curr = vec2(l, l * l);
	uint mesh_id_curr = floatBitsToUint(texelFetch(tex_vbuf_curr, ipos, 0).r);

	vec4 color_prev   = vec4(0);
	vec2 moments_prev = vec2(0);
	float sum_w       = 0.0;
	float histlen     = 0.0;
	/* bilinear interpolation, check each tap individually, renormalize
	 * afterwards */
	for(int yy = 0; yy <= 1; yy++) {
		for(int xx = 0; xx <= 1; xx++) {
			ivec2 ipos_prev    = p + ivec2(xx, yy);
			float z_prev       = texelFetch(tex_z_prev,      ipos_prev, 0).r;
			vec3  normal_prev  = texelFetch(tex_normal_prev, ipos_prev, 0).rgb;
			uint mesh_id_prev  = floatBitsToUint(texelFetch(tex_vbuf_prev, ipos_prev, 0).r);

			bool accept = true;
			accept = accept && test_inside_screen(ipos_prev, size);
			accept = accept && test_reprojected_normal(normal_curr, normal_prev);
			accept = accept && test_reprojected_depth(z_curr.z, z_prev, z_curr.y);
			accept = accept && mesh_id_prev == mesh_id_curr;

			if(accept) {
				float w = (xx == 0 ? (1.0 - w.x) : w.x)
					    * (yy == 0 ? (1.0 - w.y) : w.y);
				color_prev   += texelFetch(tex_color_prev,     ipos_prev, 0)    * w;
				moments_prev += texelFetch(tex_moments_prev,   ipos_prev, 0).rg * w;
				histlen      += texelFetch(tex_history_length, ipos_prev, 0).r  * w;
				sum_w        += w;

			}
		}
	}

#ifdef ANTILAG
	float antilag_alpha = 0.0;

	{
		vec4 v = textureLod(tex_diff_current, texC, 0);

		const float gaussian_kernel[3][3] = {
			{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 },
			{ 1.0 / 8.0,  1.0 / 4.0, 1.0 / 8.0  },
			{ 1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0 }
		};

		const int r = gradientFilterRadius;

		antilag_alpha = 0.0;
		for(int yy = -r; yy <= r; yy++) {
			for(int xx = -r; xx <= r; xx++) {
				vec4 v = texelFetch(tex_diff_current, ipos / gradientDownsample + ivec2(xx, yy), 0);
				float a = clamp(abs(v.r > 1e-4 ? abs(v.g) / v.r : 0.0), 0.0, 200.0);
				float w = 1.0 / float((2 * r + 1) * (2 * r + 1));
				antilag_alpha = max(antilag_alpha, a);
			}
		}
	}

	clamp(antilag_alpha, 0.0, 1.0);
	if(isnan(antilag_alpha))
		antilag_alpha = 1.0;

#endif


	if(sum_w > 0.01) { /* found sufficiently reliable history information */
		color_prev   /= sum_w;
		moments_prev /= sum_w;
		histlen      /= sum_w;


#ifndef ANTILAG
		const float alpha_color   = max(temporal_alpha, 1.0 / (histlen + 1.0));
		const float alpha_moments = max(0.6, 1.0 / (histlen + 1.0));
#else
		float alpha_color   = max(temporal_alpha, 1.0 / (histlen + 1.0));
		float alpha_moments = max(0.6, 1.0 / (histlen + 1.0));

		alpha_color   = mix(alpha_color,   1.0, antilag_alpha);
		alpha_moments = mix(alpha_moments, 1.0, antilag_alpha);

  #ifdef SHOW_ANTILAG_ALPHA
		imageStore(img_antilag_alpha, ipos, vec4(viridis_quintic(alpha_color), 0.0));
  #endif
#endif
		
		out_accumulated.rgb = mix(color_prev.rgb, color_curr, alpha_color);
		out_moments         = mix(moments_prev, moments_curr, alpha_moments);
		out_accumulated.a   = min(64, histlen + 1.0);
		#ifdef ANTILAG
		out_histlen         = clamp(1.0 / alpha_color, 0.0, 64.0);
		#else
		out_histlen         = min(64, histlen + 1.0);
		#endif
	}
	else {
		out_accumulated = vec4(color_curr, 1.0);
		out_moments     = moments_curr;
		out_histlen     = 1.0;
#ifdef ANTILAG
  #ifdef SHOW_ANTILAG_ALPHA
		imageStore(img_antilag_alpha, ipos, vec4(viridis_quintic(1), 0.0));
  #endif
#endif
	}

}
