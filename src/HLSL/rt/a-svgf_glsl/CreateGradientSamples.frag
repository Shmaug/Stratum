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
#include "svgf_shared.glsl"

layout(location = 0) out vec4 frag_color1;
layout(location = 1) out vec4 frag_color2;

uniform PerImageCB {
	sampler2D tex_color_unfiltered;
	usampler2D tex_gradient_samples;
	sampler2D tex_color_unfiltered_prev;
	sampler2D tex_z;
	sampler2D tex_vbuf;
	int gradientDownsample;
};

void
main()
{
	ivec2 ipos_grad = ivec2(gl_FragCoord);

	uint u = texelFetch(tex_gradient_samples, ipos_grad, 0).r;

	ivec2 tile_pos = ivec2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
	ivec2 ipos = ipos_grad * gradientDownsample + tile_pos;
	float l_curr = luminance(texelFetch(tex_color_unfiltered,      ipos,      0).rgb);
	if (u >= (1u << 31)) {
		uint idx_prev = (u >> (2 * TILE_OFFSET_SHIFT)) & ((1 << (31 - 2 * TILE_OFFSET_SHIFT)) - 1);

		int w = textureSize(tex_color_unfiltered, 0).r;
		ivec2 ipos_prev = ivec2(idx_prev % w, idx_prev / w);

		float l_prev = luminance(texelFetch(tex_color_unfiltered_prev, ipos_prev, 0).rgb);

		frag_color1.r = max(l_curr, l_prev);
		frag_color1.g = (l_curr - l_prev);
		frag_color1.b = 1;
		frag_color1.a = 0;
	}
	else {
		frag_color1.rgba = vec4(0);
	}

	vec2 moments = vec2(l_curr, l_curr * l_curr);
	float sum_w = 1.0;
	vec2 z_curr = texelFetch(tex_z, ipos, 0).rg;
	uint mesh_id  = floatBitsToUint(texelFetch(tex_vbuf, ipos, 0).r);
    for(int yy = 0; yy < gradientDownsample; yy++) {
		for(int xx = 0; xx < gradientDownsample; xx++) {
			ivec2 p = ipos_grad * gradientDownsample + ivec2(xx, yy);
			if(!all(equal(ipos, p))) {
				vec3 rgb = texelFetch(tex_color_unfiltered, p, 0).rgb;
				uint mesh_id_p = floatBitsToUint(texelFetch(tex_vbuf, p, 0).r);

				float l = luminance(rgb);

				float w = mesh_id_p == mesh_id ? 1.0 : 0.0;

				moments += vec2(l, l * l) * w;
				sum_w += w;
			}
		}
	}
	moments /= sum_w;

	float variance = max(0.0, moments[1] - moments[0] * moments[0]);

	frag_color2.r  = moments[0];
	frag_color2.g  = variance;
	frag_color2.ba = z_curr;
}
