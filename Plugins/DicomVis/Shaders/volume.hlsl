struct VolumeData {
	float4 Rotation;
	float4 InvRotation;
	float3 Scale;
	float Density;
	float3 InvScale;
	uint Mask;
	float3 Position;
	uint FrameIndex;
	float2 RemapRange;
	float2 HueRange;
	uint3 Resolution;
	uint pad;
};

#ifndef __cplusplus

#pragma compile compute BakeVolume
#pragma compile compute BakeGradient
#pragma compile compute Render

#include <math.hlsli>

[[vl::constant_id(0)]] const bool gBakedColor = false;
[[vk::constant_id(1)]] const bool gBakedGradient = false;
[[vl::constant_id(2)]] const bool gColorize = false;
[[vl::constant_id(3)]] const bool gLocalShading = false;
[[vk::constant_id(4)]] const bool gMaskColored = false; // Is the mask colored?
[[vk::constant_id(5)]] const bool gSingleChannel = true;

RWTexture3D<float4> gBakedGradient : register(u1);
RWTexture3D<uint> gMask : register(u2);
RWTexture3D<float4> Output : register(u4);

RWTexture3D<float4> gOutputVolume : register(u0);

ConstantBuffer<VolumeData> gVolume : register(b0);

RWTexture2D<float4> RenderTarget : register(u0);
Texture2DMS<float> DepthBuffer : register(t1);
Texture2D<float4> gEnvironmentTexture	: register(t2);
SamplerState Sampler : register(s3);

[[vk::push_constant]] cbuffer gPushConstants : register(b0) {
	float4x4 InvViewProj;
	uint2 ScreenResolution;
	uint2 WriteOffset;
	float3 gCameraPosition;
}

float chroma_key(float3 hsv, float3 key) {
	float3 d = abs(hsv - key) / float3(0.1, 0.5, 0.5);
	return saturate(length(d) - 1);
}

float4 sample_volume(uint3 index){
	float4 c = RWVolume[index];

	if (!gBakedColor) {
	// non-baked volume, do processing

		if (gColorize) {
			c.rgb = hsv_to_rgb(float3(HueRange.x + c.a * (HueRange.y - HueRange.x), .5, 1));

			#elif defined(NON_BAKED_RGBA)
			// chroma-key blue out (for visible human dataset)
			float3 hsv = rgb_to_hsv(c.rgb);
			c.a *= chroma_key(hsv, rgb_to_hsv(float3(0.07059, 0.07843, 0.10589))) * saturate(4 * hsv.z);
			#endif

			c.a *= saturate((c.a - RemapRange.x) / (RemapRange.y - RemapRange.x));
		}

	if (gMaskColored) {
		static const float3 maskColors[8] = {
			float3(1.0, 0.1, 0.1),
			float3(0.1, 1.0, 0.1),
			float3(0.1, 0.1, 1.0),

			float3(1.0, 1.0, 0.0),
			float3(1.0, 0.1, 1.0),
			float3(0.1, 1.0, 1.0),

			float3(1.0, 0.5, 0.1),
			float3(1.0, 0.1, 0.5),
		};
		uint value = RawMask[index] & MaskValue;
		if (value) c.rgb = maskColors[firstbitlow(value)];
	}

	return c;
}
float3 sample_gradient(uint3 index) {
	if (gBakedGradient)
		return gBakedGradient[index].xyz;
	else
		return float3(
			sample_volume(uint3(next.x , index.y, index.z)).a - sample_volume(uint3(prev.x , index.y, index.z)).a,
			sample_volume(uint3(index.x, next.y , index.z)).a - sample_volume(uint3(index.x, prev.y , index.z)).a,
			sample_volume(uint3(index.x, index.y, next.z )).a - sample_volume(uint3(index.x, index.y, prev.z )).a);
}


[numthreads(4, 4, 4)]
void BakeVolume(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	gOutputVolume[index] = sample_volume(index);
}

[numthreads(4, 4, 4)]
void BakeGradient(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	gOutputVolume[index] = float4(sample_gradient(index), 0);
}

[numthreads(8, 8, 1)]
void Render(uint3 index : SV_DispatchThreadID) {
	if (any(index.xy >= gPushConstants.ScreenResolution)) return;

	uint2 coord = gPushConstants.WriteOffset + index.xy;
	float4 screenPos = float4(2*(coord + 0.5)/float2(gPushConstants.ScreenResolution)-1, DepthBuffer.Load(coord, 1), 1);
	float4 projPos = mul(gPushConstants.InvViewProj, screenPos);
	projPos /= projPos.w;
	float3 rayDirection = normalize(projPos.xyz);
	float3 rayOrigin = gPushConstants.gCameraPosition;
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

		float4 localSample = sample_volume(idx);

		if (localSample.a > 0.001) {
			float3 gradient = sample_gradient(idx);
			float3 worldPos = qmul(VolumeRotation, uvw-0.5) * VolumeScale + VolumePosition;
			float3 worldGradient = qmul(VolumeRotation, gradient) * VolumeScale;

			if (gLocalShading)
				localSample.rgb *= saturate(dot(normalize(worldGradient), normalize(float3(1,1,-1))));
			
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

#endif