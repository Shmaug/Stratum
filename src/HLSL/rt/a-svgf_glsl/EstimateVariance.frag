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
#pragma optionNV(unroll all)

layout(location = 0) out vec4 frag_color;

uniform PerImageCB {
	sampler2D tex_color;
	sampler2D tex_moments;
	sampler2D tex_history_length;
	sampler2D tex_color_unfiltered;
	sampler2D tex_normal;
	sampler2D tex_z;
	sampler2D tex_vbuf;
};

void
main()
{
	ivec2 ipos = ivec2(gl_FragCoord);
	vec2 m = texelFetch(tex_moments, ipos, 0).rg;

	float histlen = texelFetch(tex_history_length, ipos, 0).r;

	vec4 c = texelFetch(tex_color, ipos, 0);

	const float hist_len_thresh = 4.0;

	vec3 color_spatial = vec3(0);
	vec2 moments_spatial = vec2(0);
	vec2 moments_tempora = vec2(0);
	float variance_spatial = 0.0;

	vec4 z_center = texelFetch(tex_z, ipos, 0);
	if(z_center.x < 0) {
		frag_color = vec4(c.rgb, 0);
		return;
	}

	if(histlen < hist_len_thresh) {
		{
			float l = luminance(c.rgb);
			m += vec2(l, l * l);
		}

		vec3 n_center = texelFetch(tex_normal, ipos, 0).xyz;
		uint mesh_id_center =  floatBitsToUint(texelFetch(tex_vbuf, ipos, 0).r);

		float sum_w = 1.0;
		const int r = histlen > 1 ? 2 : 3;
		for(int yy = -r; yy <= r; yy++) {
			for(int xx = -r; xx <= r; xx++) {
				if(xx != 0 || yy != 0) {
					ivec2 p = ipos + ivec2(xx, yy);
					vec4 c_p = texelFetch(tex_color, p, 0);

					float l = luminance(c.rgb);

					float z_p = texelFetch(tex_z, p, 0).x;
					vec3 n_p = texelFetch(tex_normal, p, 0).xyz;

					float w_z = abs(z_p - z_center.x) / (z_center.y * length(vec2(xx, yy)) + 1e-2);
                    float w_n = pow(max(0, dot(n_p, n_center)), 128.0); 

					uint mesh_id_p =  floatBitsToUint(texelFetch(tex_vbuf, p, 0).r);

					float w = exp(- w_z) * w_n * (mesh_id_center == mesh_id_p ? 1.0 : 0.0);

					if(isnan(w))
						w = 0.0;

					sum_w += w;

					m += vec2(l, l * l) * w;
					c.rgb += c_p.rgb * w;
				}
			}
		}

		m /= sum_w;
		c.rgb /= sum_w;
			
		moments_spatial = m;
		color_spatial = c.rgb;

		variance_spatial = (1.0 + 2.0 * (1.0 - histlen / hist_len_thresh)) * max(0.0, m.y - m.x * m.x);
		frag_color = vec4(c.rgb, (1.0 + 3.0 * (1.0 - histlen / hist_len_thresh)) * max(0.0, m.y - m.x * m.x));
	}
	else {
		float variance_temporal = max(0.0, m.y - m.x * m.x);
		frag_color = vec4(c.rgb, variance_temporal);
    }
}
