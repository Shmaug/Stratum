#pragma kernel Render

#pragma multi_compile COLORIZE BAKED
#pragma multi_compile SHADING_LOCAL

#include "common.hlsli"
#include <sampling.hlsli>

[[vk::binding(4, 0)]] RWTexture2D<float4> RenderTarget : register(u0);
[[vk::binding(5, 0)]] Texture2DMS<float> DepthBuffer : register(t0);

[[vk::binding(6, 0)]] Texture2D<float4> EnvironmentTexture	: register(t2);
[[vk::binding(7, 0)]] SamplerState Sampler : register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
	float4x4 InvViewProj;
	uint2 ScreenResolution;
	uint2 WriteOffset;
	float3 CameraPosition;
	float SampleRate;
}

float2 RayBox(float3 rayOrigin, float3 inverseRayDirection, float3 mn, float3 mx) {
	float3 t0 = (mn - rayOrigin) * inverseRayDirection;
	float3 t1 = (mx - rayOrigin) * inverseRayDirection;
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
	float4 screenPos = float4(2*(coord + 0.5)/float2(ScreenResolution)-1, DepthBuffer.Load(coord, 1), 1);
	float4 projPos = mul(InvViewProj, screenPos);
	projPos /= projPos.w;
	float3 rayDirection = normalize(projPos.xyz);
	float3 rayOrigin = CameraPosition;
	float3 depthRay = projPos.xyz;

	// world -> uvw
	rayOrigin = qmul(InvVolumeRotation, rayOrigin - VolumePosition) * InvVolumeScale + 0.5;
	rayDirection = qmul(InvVolumeRotation, rayDirection) * InvVolumeScale;
	depthRay = qmul(InvVolumeRotation, depthRay) * InvVolumeScale;
	float3 invRayDirection = 1 / rayDirection;

	// Intersect cube
	float2 isect = RayBox(rayOrigin, invRayDirection, 0, 1);
	isect.x = max(isect.x, 0);
	isect.y = min(isect.y, length(depthRay)/length(rayDirection));

	float3 texelSize = 1/float3(VolumeResolution);
	float eps = min(min(texelSize.x, texelSize.y), texelSize.z)/64;

	texelSize *= sign(rayDirection);

	float4 sum = 0;
	uint sampleCount = 0;
	for (float t = isect.x; t <= isect.y && sum.a < 0.99;) {
		float3 uvw = saturate(rayOrigin + rayDirection * t);
		uint3 idx = min(uint3(uvw * float3(VolumeResolution)), VolumeResolution-1);
		
		// intersect the current texel
		float2 ti = RayBox(rayOrigin, invRayDirection, uvw, uvw + texelSize);
		float dt = ti.y - ti.x;
		if (dt <= 0) break;

		float4 localSample = SampleColor(idx);

		if (localSample.a > 0.001) {
			float3 gradient = SampleGradient(idx);
			float3 worldPos = qmul(VolumeRotation, uvw-0.5) * VolumeScale + VolumePosition;
			float3 worldGradient = qmul(VolumeRotation, gradient) * VolumeScale;

			#ifdef SHADING_LOCAL
				localSample.rgb *= saturate(dot(normalize(worldGradient), normalize(float3(1,1,-1))));
			#endif
				// front-to-back alpha blending
				localSample.a = saturate(localSample.a*Density);
				localSample.rgb *= localSample.a;
				sum += (1 - sum.a) * localSample;
		}

		t = ti.y + eps;
		sampleCount++;
	}

	float4 src = RenderTarget[coord];
	RenderTarget[coord] = float4(src.rgb * (1 - sum.a) + sum.rgb * sum.a, 1);
}