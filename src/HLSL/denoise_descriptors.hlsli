#ifndef PT_DESCRIPTORS_H
#define PT_DESCRIPTORS_H

#include "scene.hlsli"

[[vk::binding(0,0)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(1,0)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(2,0)]] RWTexture2D<float4> gRadiance;
[[vk::binding(3,0)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(4,0)]] RWTexture2D<float4> gAccumColor;
[[vk::binding(5,0)]] RWTexture2D<float2> gAccumMoments;
[[vk::binding(6,0)]] RWTexture2D<float4> gFilterImages[2];
[[vk::binding(7,0)]] SamplerState gSampler1;
[[vk::binding(8,0)]] RWStructuredBuffer<VisibilityInfo> gPrevVisibility;
[[vk::binding(9,0)]] Texture2D<float4> gPrevRadiance;
[[vk::binding(10,0)]] Texture2D<float4> gPrevAlbedo;
[[vk::binding(11,0)]] Texture2D<float4> gPrevAccumColor;
[[vk::binding(12,0)]] Texture2D<float2> gPrevAccumMoments;
[[vk::binding(13,0)]] StructuredBuffer<uint> gInstanceIndexMap;

#endif