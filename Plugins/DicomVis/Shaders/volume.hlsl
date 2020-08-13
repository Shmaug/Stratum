#pragma kernel Render

#pragma multi_compile MASK_COLOR
#pragma multi_compile NON_BAKED_RGBA NON_BAKED_R NON_BAKED_R_COLORIZE
#pragma multi_compile GRADIENT_TEXTURE
#pragma multi_compile LIGHTING

#ifndef PI
#define PI (3.1415926535897932)
#endif
#ifndef INV_PI
#define INV_PI (1.0 / PI)
#endif

[[vk::binding(0, 0)]] RWTexture2D<float4> RenderTarget : register(u0);
[[vk::binding(1, 0)]] Texture2DMS<float> DepthBuffer : register(t0);

#if defined(NON_BAKED_R) || defined (NON_BAKED_R_COLORIZE)
[[vk::binding(2, 0)]] Texture3D<float> Volume : register(t0);
#else
[[vk::binding(2, 0)]] Texture3D<float4> Volume : register(t0);
#endif
[[vk::binding(3, 0)]] RWTexture3D<float4> Gradient : register(u2);
[[vk::binding(4, 0)]] Texture3D<uint> RawMask : register(t1);

[[vk::binding(5, 0)]] Texture2D<float4> EnvironmentTexture	: register(t2);

[[vk::binding(6, 0)]] Texture2D<float4> NoiseTex : register(t3);
[[vk::binding(7, 0)]] SamplerState Sampler : register(s0);

#include "common.h"

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
	float4x4 InvViewProj;
	uint2 ScreenResolution;
	uint2 WriteOffset;
	float3 CameraPosition;
	float CameraNear;
	float CameraFar;
}

#include <include/sampling.hlsli>

float2 RayBox(float3 rayOrigin, float3 rayDirection, float3 mn, float3 mx) {
	float3 id = 1 / rayDirection;
	float3 t0 = (mn - rayOrigin) * id;
	float3 t1 = (mx - rayOrigin) * id;
	float3 tmin = min(t0, t1);
	float3 tmax = max(t0, t1);
	return float2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

float3 qmul(float4 q, float3 vec) {
	return 2 * dot(q.xyz, vec) * q.xyz + (q.w * q.w - dot(q.xyz, q.xyz)) * vec + 2 * q.w * cross(q.xyz, vec);
}

[numthreads(8, 8, 1)]
void Render(uint3 index : SV_DispatchThreadID) {
	if (any(index.xy >= ScreenResolution)) return;
	uint2 coord = WriteOffset + index.xy;
	
	float2 uv = (coord + 0.5) / float2(ScreenResolution);
	float4 unprojected = mul(InvViewProj, float4(2*uv-1, 0, 1));
	float3 rayDirection = normalize(unprojected.xyz / unprojected.w);
	float3 rayOrigin = CameraPosition;

	// Move to object space, but without scale
	rayOrigin = qmul(InvVolumeRotation, rayOrigin - VolumePosition);
	rayDirection = qmul(InvVolumeRotation, rayDirection);

	// Intersect cube
	float2 isect = RayBox(rayOrigin, rayDirection, -VolumeScale/2, VolumeScale/2);
	isect.x = max(0, isect.x);
	// Sample depth buffer
	isect.y = min(isect.y, CameraNear + (CameraFar - CameraNear) * DepthBuffer.Load(coord, 0));

	// early exit
	if (isect.x >= isect.y) return;

	// jitter samples / init rng
	uint idx = index.y * ScreenResolution.x + index.x;
	uint rnd = 0xFFFF * NoiseTex.Load(uint3((idx / (4 * 256)) % 256, (idx / 4) % 256, 0))[idx % 4];
	RandomSampler rng;
	rng.index = FrameIndex % (CMJ_DIM * CMJ_DIM);
	rng.dimension = 1;
	rng.scramble = rnd * 0x1fe3434f * ((FrameIndex + 133 * rnd) / (CMJ_DIM * CMJ_DIM));
	isect.x -= StepSize * SampleRNG(rng).x;
	
	float4 sum = 0;
	
	for (float t = isect.x; t < isect.y; t += StepSize) {
		float3 p = rayOrigin + rayDirection * t;

		// SampleColor is defined in common.hlsli
		// It samples the volume and does any processing required
		float4 localSample = SampleColor(p * InvVolumeScale + 0.5);

		#ifdef LIGHTING
		float3 gradient = qmul(VolumeRotation, SampleGradient(p)) * VolumeScale;
		float l = length(gradient);
		if (l > .001)
			localSample.rgb *= saturate(1 - l) * max(0, dot(float3(.57735, .57735, .57735), gradient / l));
		#endif
		
		localSample.a *= StepSize * Density;
		localSample.a = saturate(localSample.a);

		// front-to-back alpha blending
		localSample.rgb *= localSample.a;
		sum += (1 - sum.a) * localSample;
	}
	
	RenderTarget[coord] = float4(RenderTarget[coord].rgb * (1 - sum.a) + sum.rgb * sum.a, 1);
}