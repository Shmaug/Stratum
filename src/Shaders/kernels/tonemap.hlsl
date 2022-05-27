#pragma compile dxc -spirv -T cs_6_7 -E main

#include <common.h>
#include <tonemap.h>

// extended Reinhard
static const float max_white = 1;

// Uncharted 2
static const float W = 11.2;
static const float exposure_bias = 2.0f;


float3 tonemap_reinhard(const float3 c) {
	const float l = luminance(c);
	const float l1 = l / (1 + l);
	return c * (1 + c);
}
float3 tonemap_reinhard_extended(const float3 c) {
	return c * (1 + c / pow2(max_white)) / (1 + c);
}

float3 tonemap_reinhard_luminance(const float3 c) {
	const float l = luminance(c);
	const float l1 = l / (1 + l);
	return c * (l1 / l);
}
float3 tonemap_reinhard_luminance_extended(const float3 c) {
	const float l = luminance(c);
	const float l1 = l * (1 + l / pow2(max_white)) / (1 + l);
	return c * (l1 / l);
}

float3 tonemap_uncharted2_partial(const float3 x) {
  	static const float A = 0.15;
  	static const float B = 0.50;
  	static const float C = 0.10;
  	static const float D = 0.20;
  	static const float E = 0.02;
  	static const float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}
float3 tonemap_uncharted2(float3 c) {
	return tonemap_uncharted2_partial(exposure_bias*c) / tonemap_uncharted2_partial(W);
}

float3 tonemap_filmic(float3 c) {
	c = max(0, c - 0.004f);
	return (c * (6.2f * c + 0.5f)) / (c * (6.2f * c + 1.7f) + 0.06f);
}

float3 rtt_and_odt_fit(float3 v) {
    const float3 a = v * (v + 0.0245786f) - 0.000090537f;
    const float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}
float3 aces_fitted(float3 v) {
	// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
	static const float3x3 aces_input_matrix = {
		{ 0.59719, 0.35458, 0.04823 },
		{ 0.07600, 0.90834, 0.01566 },
		{ 0.02840, 0.13383, 0.83777 }
	};
	// ODT_SAT => XYZ => D60_2_D65 => sRGB
	static const float3x3 aces_output_matrix = {
		{  1.60475, -0.53108, -0.07367 },
		{ -0.10208,  1.10813, -0.00605 },
		{ -0.00327, -0.07276,  1.07602 }
	};
    v = mul(aces_input_matrix, v);
    v = rtt_and_odt_fit(v);
    return saturate(mul(aces_output_matrix, v));
}

float3 aces_approx(float3 v) {
    static const float a = 2.51f;
    static const float b = 0.03f;
    static const float c = 2.43f;
    static const float d = 0.59f;
    static const float e = 0.14f;
    v *= 0.6f;
    return saturate((v*(a*v+b))/(v*(c*v+d)+e));
}

[[vk::constant_id(0)]] const uint gMode = 0;
[[vk::constant_id(1)]] const bool gModulateAlbedo = true;
[[vk::constant_id(2)]] const bool gGammaCorrection = true;

Texture2D<float4> gInput;
RWTexture2D<float4> gOutput;
Texture2D<float4> gAlbedo;

[[vk::push_constant]] const struct {
	float gExposure;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gOutput.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float3 radiance = gInput[index.xy].rgb;
	const float3 albedo = gAlbedo[index.xy].rgb;
	if (gModulateAlbedo) {
		if (albedo.r > 0) radiance.r *= albedo.r;
		if (albedo.g > 0) radiance.g *= albedo.g;
		if (albedo.b > 0) radiance.b *= albedo.b;
	}

	radiance *= gPushConstants.gExposure;
	switch (gMode) {
	case (uint)TonemapMode::eReinhard:
		radiance = tonemap_reinhard(radiance);
		break;
	case (uint)TonemapMode::eReinhardExtended:
		radiance = tonemap_reinhard_extended(radiance);
		break;
	case (uint)TonemapMode::eReinhardLuminance:
		radiance = tonemap_reinhard_luminance(radiance);
		break;
	case (uint)TonemapMode::eReinhardLuminanceExtended:
		radiance = tonemap_reinhard_luminance_extended(radiance);
		break;
	case (uint)TonemapMode::eUncharted2:
		radiance = tonemap_uncharted2(radiance);
		break;
	case (uint)TonemapMode::eFilmic:
		radiance = tonemap_filmic(radiance);
		break;
	case (uint)TonemapMode::eACES:
		radiance = aces_fitted(radiance);
		break;
	case (uint)TonemapMode::eACESApprox:
		radiance = aces_approx(radiance);
		break;
	}
	if (gGammaCorrection) radiance = rgb_to_srgb(radiance);

	gOutput[index.xy] = float4(radiance, 1);
}