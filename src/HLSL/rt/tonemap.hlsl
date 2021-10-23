#pragma compile dxc -spirv -T cs_6_7 -E main

#include "../ACES.hlsli"
#include "a-svgf/svgf_shared.hlsli"

[[vk::constant_id(0)]] const uint gModulateAlbedo = false;
[[vk::constant_id(1)]] const uint gDebugMode = 0;

[[vk::binding(0)]] RWTexture2D<float4> gColor;
[[vk::binding(1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(2)]] Texture2D<uint> gGradientSamples;
[[vk::binding(3)]] Texture2D<float> gHistoryLength;

[[vk::push_constant]] const struct {
	float gExposure;
	float gGamma;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gColor.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float4 radiance = gModulateAlbedo ? gColor[index.xy] * gAlbedo[index.xy] : gColor[index.xy];
	radiance.rgb = ACES_fitted(radiance.rgb);
	radiance.rgb = pow(radiance.rgb*gPushConstants.gExposure, 1/gPushConstants.gGamma);

	if (gDebugMode == 1) {
		uint u = gGradientSamples[index.xy/gGradientDownsample];
		uint2 tile_pos = uint2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
		uint2 ipos = (index.xy/gGradientDownsample)*gGradientDownsample + tile_pos;
		if (u >= (1u << 31) && all(ipos == index.xy))
			radiance.rgb = float3(1,0,1);
	} else if (gDebugMode == 2) {
		radiance = gHistoryLength[index.xy]/64;
	}

	gColor[index.xy] = radiance;
}