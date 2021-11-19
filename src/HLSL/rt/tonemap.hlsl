#pragma compile dxc -spirv -T cs_6_7 -E main

#define DEBUG_GRADIENT_DIFF0 1
#define DEBUG_GRADIENT_DIFF1 2

#include "a-svgf/svgf_shared.hlsli"
#include "../math.hlsli"

[[vk::constant_id(0)]] const bool gModulateAlbedo = false;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::constant_id(2)]] const int gGradientFilterRadius = 2;

[[vk::binding(0)]] RWTexture2D<float4> gInput;
[[vk::binding(1)]] RWTexture2D<float4> gOutput;
[[vk::binding(2)]] RWTexture2D<float4> gAlbedo;

[[vk::binding(3)]] Texture2D<float2> gDiff;
[[vk::binding(4)]] Texture2D<float4> gDebug1;
[[vk::binding(5)]] Texture2D<float> gDebug2;

[[vk::push_constant]] const struct {
	float gExposure;
	float gGamma;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gOutput.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float3 radiance = gInput[index.xy].rgb;
	if (gModulateAlbedo) {
		float3 albedo = gAlbedo[index.xy].rgb;
		if (albedo.r > 0) radiance.r *= albedo.r;
		if (albedo.g > 0) radiance.g *= albedo.g;
		if (albedo.b > 0) radiance.b *= albedo.b;
	}
	radiance = pow(radiance*gPushConstants.gExposure, 1/gPushConstants.gGamma);
	
	switch (gDebugMode) {
	case 1: {
		float2 v = gDiff[index.xy/gGradientDownsample];
		radiance = viridis_quintic(saturate(v.r > 1e-4 ? abs(v.g)/v.r : 0));
		break;
	}
	case 2:
		radiance = viridis_quintic(gDebug1[index.xy/gGradientDownsample].g);
		break;
	case 3: // antilag alpha
		radiance = viridis_quintic(gDebug2[index.xy]);
		break;
	case 4: // accum length
		radiance = viridis_quintic(saturate(gDebug2[index.xy]/128.f));
		break;
	case 5: { // dzdx, dzdy
		float2 z = gDebug1[index.xy].zw;
		radiance = viridis_quintic(saturate(max(z.x, z.y)));
		break;
	}
	case 6: // normal
		radiance = gDebug1[index.xy].rgb*.5 + .5;
		break;
	}

	gOutput[index.xy] = float4(radiance, 1);
}