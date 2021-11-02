#pragma compile dxc -spirv -T cs_6_7 -E main

#include "../ACES.hlsli"
#include "a-svgf/svgf_shared.hlsli"

[[vk::constant_id(0)]] const bool gModulateAlbedo = false;
[[vk::constant_id(1)]] const bool gACES = false;
[[vk::constant_id(2)]] const uint gDebugMode = 0;
[[vk::constant_id(3)]] const int gGradientFilterRadius = 2;

[[vk::binding(0)]] RWTexture2D<float4> gColor;
[[vk::binding(1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(2)]] Texture2D<uint> gGradientPositions;
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
	gColor.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float4 radiance = gModulateAlbedo ? gColor[index.xy] * gAlbedo[index.xy] : gColor[index.xy];
	if (gACES) radiance.rgb = ACES_fitted(radiance.rgb);

	switch (gDebugMode) {
	case 0:
		break;
	case 1:
		radiance = float4(gDebug1[index.xy/gGradientDownsample].ggg, 0);
		break;
	case 2:
	case 3:
		radiance = gDebug2[index.xy];
		break;
	default: {
		uint2 gradPos = index.xy/gGradientDownsample;
		uint u = gGradientPositions[gradPos];
		uint2 tile_pos = uint2((u & TILE_OFFSET_MASK), (u >> TILE_OFFSET_SHIFT) & TILE_OFFSET_MASK);
		uint2 ipos = gradPos*gGradientDownsample + tile_pos;
		if (u >= (1u << 31)) {
			uint idx_prev = (u >> (2u * TILE_OFFSET_SHIFT)) & ((1u << (31u - 2u * TILE_OFFSET_SHIFT)) - 1u);
			uint2 ipos_prev = uint2(idx_prev % resolution.x, idx_prev / resolution.x);
			switch (gDebugMode) {
			case 5: {
				float2 v = gDiff[gradPos];
				radiance.rgb = lerp(float3(0,1,0), float3(1,0,0), saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0));
				break;
			}
			case 6: {
				float antilag_alpha = 0;
				if (gGradientFilterRadius == 0) {
					float2 v = gDiff[ipos/gGradientDownsample];
					antilag_alpha = saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0);
				} else {
					for (int yy = -gGradientFilterRadius; yy <= gGradientFilterRadius; yy++)
						for (int xx = -gGradientFilterRadius; xx <= gGradientFilterRadius; xx++) {
							int2 p = int2(ipos/gGradientDownsample) + int2(xx, yy);
							if (any(p < 0) || any(p >= resolution/gGradientDownsample)) continue;
							float2 v = gDiff[p];
							antilag_alpha = max(antilag_alpha, saturate(v.r > 1e-4 ? abs(v.g) / v.r : 0));
						}
				}
				if (isnan(antilag_alpha) || isinf(antilag_alpha)) antilag_alpha = 1;
				radiance.rgb = lerp(float3(0,1,0), float3(1,0,0), antilag_alpha);
				break;
			}
			}
		} else
			radiance.rgb = 0;
		break;
	}
	}

	radiance.rgb = pow(radiance.rgb*gPushConstants.gExposure, 1/gPushConstants.gGamma);
	gColor[index.xy] = radiance;
}