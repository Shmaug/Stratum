#pragma compile dxc -spirv -T cs_6_7 -E demodulate_albedo
#pragma compile dxc -spirv -T cs_6_7 -E main

#include "../tonemap.hlsli"
#include "a-svgf/svgf_shared.hlsli"

float3 tonemap_reinhard(const float3 color) {
	return color/(1+color);
}

float3 tonemap_uncharted2(const float3 x) {
  const float A = 0.15;
  const float B = 0.50;
  const float C = 0.10;
  const float D = 0.20;
  const float E = 0.02;
  const float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 tonemap_uc2(float3 color) {
	const float W = 11.2;
	const float exposure_bias = 2.0f;
	const float3 curr = tonemap_uncharted2(exposure_bias*color);
	return curr/tonemap_uncharted2(W);
}

float3 tonemap_filmic(float3 color) {
	color = max(0, color - 0.004f);
	return (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f) + 0.06f);
}

[[vk::constant_id(0)]] const uint gMode = 0;
[[vk::constant_id(1)]] const bool gModulateAlbedo = true;
[[vk::constant_id(2)]] const bool gGammaCorrection = true;
[[vk::constant_id(3)]] const uint gDebugMode = 0;
[[vk::constant_id(4)]] const uint gGradientDownsample = 3u;

Texture2D<float4> gInput;
RWTexture2D<float4> gOutput;
Texture2D<float4> gAlbedo;

Texture2D<float4> gDebug1;
Texture2D<float2> gDebug2;

[[vk::push_constant]] const struct {
	float gExposure;
} gPushConstants;

[numthreads(8,8,1)]
void demodulate_albedo(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gOutput.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;
	const float3 albedo = gAlbedo[index.xy].rgb;
	float4 radiance = gOutput[index.xy];
	if (albedo.r > 0) radiance.r /= albedo.r;
	if (albedo.g > 0) radiance.g /= albedo.g;
	if (albedo.b > 0) radiance.b /= albedo.b;
	gOutput[index.xy] = radiance;
}

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gOutput.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float3 radiance = gInput[index.xy].rgb;
	const float3 albedo = gAlbedo[index.xy].rgb;
	if (gModulateAlbedo && gDebugMode != DebugMode::eDemodulatedRadiance) {
		if (albedo.r > 0) radiance.r *= albedo.r;
		if (albedo.g > 0) radiance.g *= albedo.g;
		if (albedo.b > 0) radiance.b *= albedo.b;
	}
	if (gDebugMode == DebugMode::eAlbedo)
		radiance = gAlbedo[index.xy].rgb;
	
	switch (gDebugMode) {
		default:
			radiance *= gPushConstants.gExposure;
			switch (gMode) {
			case TonemapMode::eReinhard:
				radiance = tonemap_reinhard(radiance);
				break;
			case TonemapMode::eUncharted2:
				radiance = tonemap_uc2(radiance);
				break;
			case TonemapMode::eFilmic:
				radiance = tonemap_filmic(radiance);
				break;
			}
			if (gGammaCorrection) radiance = rgb_to_srgb(radiance);
			break;
			
		case DebugMode::eAccumLength:
			radiance = viridis_quintic(saturate(gDebug1[index.xy].a*gPushConstants.gExposure));
			break;
		case DebugMode::eTemporalGradient:
			radiance = viridis_quintic(saturate(abs(gDebug2[index.xy/gGradientDownsample].g)*gPushConstants.gExposure));
			break;
		case DebugMode::eRelativeTemporalGradient: {
			const float2 diff1 = gDebug2[index.xy/gGradientDownsample];
			radiance = viridis_quintic(saturate((diff1.r > 1e-4 ? abs(diff1.g)/diff1.r : 0)*gPushConstants.gExposure));
			break;
		}
	}

	gOutput[index.xy] = float4(radiance, 1);
}