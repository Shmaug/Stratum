#ifndef DENOISER_H
#define DENOISER_H

#include "common.h"

#ifdef __cplusplus
namespace stm {
#endif

enum class DenoiserDebugMode {
	eNone,
	eSampleCount,
	eVariance,
	eWeightSum,
	eDebugModeCount
};

#ifdef __cplusplus
}
namespace std {
inline string to_string(const stm::DenoiserDebugMode& m) {
switch (m) {
	default: return "Unknown";
	case stm::DenoiserDebugMode::eNone: return "None";
	case stm::DenoiserDebugMode::eSampleCount: return "Sample Count";
	case stm::DenoiserDebugMode::eVariance: return "Variance";
	case stm::DenoiserDebugMode::eWeightSum: return "Weight Sum";
}
};
}
#endif


#ifdef __HLSL__

#include "scene.h"

[[vk::binding( 0,0)]] StructuredBuffer<ViewData> gViews;
[[vk::binding( 1,0)]] StructuredBuffer<uint> gInstanceIndexMap;
[[vk::binding( 2,0)]] StructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding( 3,0)]] StructuredBuffer<VisibilityInfo> gPrevVisibility;
[[vk::binding( 4,0)]] Texture2D<float2> gPrevUVs;
[[vk::binding( 5,0)]] Texture2D<float4> gRadiance;
[[vk::binding( 6,0)]] Texture2D<float4> gAlbedo;
[[vk::binding( 7,0)]] RWTexture2D<float4> gAccumColor;
[[vk::binding( 8,0)]] RWTexture2D<float2> gAccumMoments;
[[vk::binding( 9,0)]] RWTexture2D<float4> gFilterImages[2];
[[vk::binding(11,0)]] Texture2D<float4> gPrevRadiance;
[[vk::binding(12,0)]] Texture2D<float4> gPrevAccumColor;
[[vk::binding(13,0)]] Texture2D<float2> gPrevAccumMoments;
[[vk::binding(14,0)]] SamplerState gSampler;
[[vk::binding(15,0)]] RWTexture2D<float4> gDebugImage;

#endif

#endif