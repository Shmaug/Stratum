#pragma compile -D -S comp -e bake_color
#pragma compile -D -S comp -e bake_gradient
#pragma compile -D -S comp -e render

#include "../../Shaders/include/transform.hlsli"

[[vk::constant_id(0)]] const bool gBakedColor = false;
[[vk::constant_id(1)]] const bool gBakedGradient = false;
[[vk::constant_id(2)]] const bool gColorize = false;
[[vk::constant_id(3)]] const bool gLocalShading = false;
[[vk::constant_id(4)]] const bool gMaskColored = false;
[[vk::constant_id(5)]] const bool gSingleChannel = true;
[[vk::constant_id(6)]] const uint gMaxSamples = 4096;

Texture3D<float4> gVolume;
Texture3D<uint> gMask;
Texture3D<float4> gGradient;
RWTexture3D<float4> gVolumeRW;

RWTexture2D<float4> gRenderTarget;
Texture2DMS<float> gDepthBuffer;

[[vk::push_constant]] cbuffer {
	TransformData gCameraToWorld;
	ProjectionData gProjection;
	TransformData gVolumeTransform;
	uint2 gScreenResolution;
	uint2 gWriteOffset;
	uint3 gVolumeResolution;
	float gDensity;
	uint gMaskValue;
	uint gFrameIndex;
	float2 gRemapRange;
	float2 gHueRange;
};

float4 sample_volume(uint3 index) {
	float4 c = gVolume[index];

	if (!gBakedColor) {
		// non-baked volume, do processing
		if (gColorize)
			c.rgb = hsv_to_rgb(float3(gHueRange.x + c.a * (gHueRange.y - gHueRange.x), .5, 1));
	
		c.a *= saturate((c.a - gRemapRange.x) / (gRemapRange.y - gRemapRange.x));
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
		uint value = gMask[index] & gMaskValue;
		if (value) c.rgb *= maskColors[firstbitlow(value)];
	}

	return c;
}
float3 sample_gradient(uint3 index) {
	if (gBakedGradient)
		return gGradient[index].xyz;
	else {
		uint3 next = min(index + 1, gVolumeResolution);
		uint3 prev = uint3(max(0, int3(index) - 1));
		return float3(
			sample_volume(uint3(next.x , index.y, index.z)).a - sample_volume(uint3(prev.x , index.y, index.z)).a,
			sample_volume(uint3(index.x, next.y , index.z)).a - sample_volume(uint3(index.x, prev.y , index.z)).a,
			sample_volume(uint3(index.x, index.y, next.z )).a - sample_volume(uint3(index.x, index.y, prev.z )).a );
	}
}

[numthreads(4, 4, 4)]
void bake_color(uint3 index : SV_DispatchThreadID) {
	if (any(index >= gVolumeResolution)) return;
	gVolumeRW[index] = sample_volume(index);
}

[numthreads(4, 4, 4)]
void bake_gradient(uint3 index : SV_DispatchThreadID) {
	if (any(index >= gVolumeResolution)) return;
	gVolumeRW[index] = float4(sample_gradient(index), 0);
}

[numthreads(8, 8, 1)]
void render(uint3 index : SV_DispatchThreadID) {
	if (any(index.xy >= gScreenResolution)) return;

	uint2 coord = gWriteOffset + index.xy;
	float4 screenPos = float4(2*(coord + 0.5)/float2(gScreenResolution)-1, gDepthBuffer.Load(coord, 1), 1);
	float3 depthRay = transform_vector(gCameraToWorld, back_project(gProjection, screenPos));
	float3 rayDirection = normalize(depthRay);
	float3 rayOrigin = gCameraToWorld.Translation;

	// world -> uvw
	rayOrigin = transform_point(gVolumeTransform, rayOrigin);
	rayDirection = transform_vector(gVolumeTransform, rayDirection);
	depthRay = transform_vector(gVolumeTransform, depthRay);

	float3 invRayDirection = 1 / rayDirection;

	// Intersect cube
	float2 isect = ray_box(rayOrigin, invRayDirection, 0, 1);
	isect.x = max(isect.x, 0);
	isect.y = min(isect.y, length(depthRay)/length(rayDirection));

	float3 texelSize = 1/float3(gVolumeResolution);
	float eps = min(min(texelSize.x, texelSize.y), texelSize.z)/4;

	float4 sum = 0;
	uint sampleCount = 0;
	float t = isect.x;
	while (sampleCount < gMaxSamples && t <= isect.y && sum.a < 0.99) {
		float3 uvw = saturate(rayOrigin + rayDirection * t);
		uint3 idx = min(uint3(uvw * float3(gVolumeResolution)), gVolumeResolution-1);
		
		float4 localSample = sample_volume(idx);

		if (localSample.a > 0.001) {
			float3 gradient = sample_gradient(idx);
			float3 worldPos = transform_point(gVolumeTransform, uvw);
			float3 worldGradient = transform_vector(gVolumeTransform, gradient);

			if (gLocalShading) {
				const float3 gLightDir = normalize(float3(1,1,-1));
				localSample.rgb *= saturate(dot(normalize(worldGradient), gLightDir ));
			}

			// front-to-back alpha blending
			localSample.a = saturate(localSample.a*gDensity);
			localSample.rgb *= localSample.a;
			sum += (1 - sum.a) * localSample;
		}

		t += eps;
		sampleCount++;
	}

	float4 src = gRenderTarget[coord];
	gRenderTarget[coord] = float4(src.rgb * (1 - sum.a) + sum.rgb * sum.a, 1);
}