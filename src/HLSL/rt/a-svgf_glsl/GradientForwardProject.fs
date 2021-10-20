#version 450
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

#extension GL_ARB_bindless_texture : enable

#include "HostDeviceData.h"
#include "svgf_shared.glsl"

#pragma optionNV(unroll all)

uniform PerFrameCB
{
	int max_num_model_instances;
	usampler2D tex_gradient_samples_prev;
	layout(r32ui) uimage2D img_gradient_samples;

	sampler2D tex_pos_ms_prev;
	layout(rgba32f) image2D img_pos_ms;

	usampler2D tex_rng_seed_prev;
	layout(r32ui) uimage2D img_rng_seed;

	sampler2D tex_visibility_buffer_prev;
	layout(rgba32f) image2D img_visibility_buffer;

	sampler2D tex_uv_deriv_prev;
	layout(rgba32f) image2D img_uv_deriv;

	sampler2D tex_albedo_prev;
	layout(rgba32f) image2D img_albedo;

	sampler2D tex_z;
	sampler2D tex_z_prev;

	sampler2D tex_normal;
	sampler2D tex_normal_prev;

	int gradientDownsample;
	uint frameNum;
};

layout(binding = 0) buffer BufferInstanceMatrices
{
	mat4 mdinstance_trans_mat[];
};

void
encrypt_tea(inout uvec2 arg)
{
        const unsigned int key[] = {
                0xa341316c, 0xc8013ea4, 0xad90777d, 0x7e95761e
        };
        unsigned int v0 = arg[0], v1 = arg[1];
        unsigned int sum = 0;
        unsigned int delta = 0x9e3779b9;

        for(int i = 0; i < 16; i++) { // XXX rounds reduced, carefully check if good
        //for(int i = 0; i < 32; i++) {
                sum += delta;
                v0 += ((v1 << 4) + key[0]) ^ (v1 + sum) ^ ((v1 >> 5) + key[1]);
                v1 += ((v0 << 4) + key[2]) ^ (v0 + sum) ^ ((v0 >> 5) + key[3]);
        }
        arg[0] = v0;
        arg[1] = v1;
}


void
main()
{
	ivec2 idx_prev;
	{
		ivec2 ipos = ivec2(gl_FragCoord);
		uvec2 arg = uvec2(ipos.x + ipos.y * textureSize(tex_rng_seed_prev, 0).x, frameNum);
		encrypt_tea(arg);
		arg %= gradientDownsample;
		idx_prev = ivec2(ipos * gradientDownsample + arg);
	}

	precise vec4 pos_ms_prev = texelFetch(tex_pos_ms_prev, idx_prev, 0);

	uint matrix_idx = floatBitsToUint(pos_ms_prev.w);

	precise mat4 mvp = mdinstance_trans_mat[matrix_idx];

	precise vec4 pos_cs_curr = mvp * vec4(pos_ms_prev.xyz, 1.0);
	/* check whether gradient sample projects outside the screen */
	if(any(lessThanEqual(pos_cs_curr.xyz, -pos_cs_curr.www))
	|| any(greaterThanEqual(pos_cs_curr.xyz, pos_cs_curr.www)))
	{
		return;
	}
	pos_cs_curr.xyz /= pos_cs_curr.w;
	pos_cs_curr.xyz *= 0.5;
	pos_cs_curr.xyz += 0.5;
	
	ivec2 idx_curr = ivec2(pos_cs_curr.xy * textureSize(tex_rng_seed_prev, 0).xy);

	vec4 z_curr, z_prev;
	z_curr = texelFetch(tex_z,      idx_curr, 0);
	z_prev = texelFetch(tex_z_prev, idx_prev, 0);

	bool accept = true;
	accept = accept && test_reprojected_depth(z_curr.z, z_prev.x, z_curr.y);

	vec3 normal_curr, normal_prev;
	normal_curr = texelFetch(tex_normal,      idx_curr, 0).xyz;
	normal_prev = texelFetch(tex_normal_prev, idx_prev, 0).xyz;
	accept = accept && (dot(normal_curr, normal_prev) > 0.9);
	if(!accept) return;

	ivec2 tile_pos_curr = idx_curr / gradientDownsample;
	uint gradient_idx_curr = get_gradient_idx_from_tile_pos(idx_curr % gradientDownsample);

	/* encode position in previous frame */
	gradient_idx_curr |= (idx_prev.x + idx_prev.y * textureSize(tex_rng_seed_prev, 0).x) << (2 * TILE_OFFSET_SHIFT);

	uint res = imageAtomicCompSwap(img_gradient_samples, tile_pos_curr, 0u, gradient_idx_curr);
	if(res == 0)
	{
		uint rng_seed_prev = texelFetch(tex_rng_seed_prev, idx_prev, 0).x;
		imageStore(img_rng_seed, idx_curr, uvec4(rng_seed_prev));

		vec4 v_prev = texelFetch(tex_visibility_buffer_prev, idx_prev, 0);
		imageStore(img_visibility_buffer, idx_curr, v_prev);

		vec4 uv_prev = texelFetch(tex_uv_deriv_prev, idx_prev, 0);
		imageStore(img_uv_deriv, idx_curr, uv_prev);

		vec4 albedo_prev = texelFetch(tex_albedo_prev, idx_prev, 0);
		imageStore(img_albedo, idx_curr, albedo_prev);

		imageStore(img_pos_ms, idx_curr, pos_ms_prev);
	}
}
