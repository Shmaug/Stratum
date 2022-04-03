#ifndef PT_DESCRIPTORS_H
#define PT_DESCRIPTORS_H

#include "../scene.hlsli"

#ifdef PT_DESCRIPTOR_SET_0

#define gVolumeCount 8
#define gImageCount 1024

[[vk::binding(0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding(2,0)]] ByteAddressBuffer gIndices;
[[vk::binding(3,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding(4,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(5,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding(6,0)]] RWByteAddressBuffer gCounters;
[[vk::binding(7,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(8,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(9,0)]] SamplerState gSampler;
[[vk::binding(10,0)]] Texture2D<float4> gImages[gImageCount];

#endif

#ifdef PT_DESCRIPTOR_SET_1

#include "../reservoir.hlsli"
#include "../path_state.hlsli"

[[vk::binding(0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding(2,1)]] StructuredBuffer<uint> gViewVolumeInstances;
[[vk::binding(3,1)]] StructuredBuffer<uint> gInstanceIndexMap;
[[vk::binding(4,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding(5,1)]] Texture2D<float4> gPrevRadiance;
[[vk::binding(6,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(7,1)]] Texture2D<float4> gPrevAlbedo;
[[vk::binding(8,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(9,1)]] RWStructuredBuffer<Reservoir> gPrevReservoirs;
[[vk::binding(10,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(11,1)]] RWStructuredBuffer<PathVertexGeometry> gPathVertices;
[[vk::binding(12,1)]] RWStructuredBuffer<MaterialSampleRecord> gMaterialSamples;
[[vk::binding(13,1)]] RWTexture2D<float4> gAccumColor;
[[vk::binding(14,1)]] Texture2D<float4> gPrevAccumColor;
[[vk::binding(15,1)]] RWTexture2D<float2> gAccumMoments;
[[vk::binding(16,1)]] Texture2D<float2> gPrevAccumMoments;
[[vk::binding(17,1)]] RWTexture2D<uint> gGradientSamples;
[[vk::binding(18,1)]] RWTexture2D<float4> gFilterImages[2];
[[vk::binding(20,1)]] RWTexture2D<float2> gDiffImage1[2];
[[vk::binding(22,1)]] RWTexture2D<float4> gDiffImage2[2];
[[vk::binding(24,0)]] SamplerState gSampler1;

#define DECLARE_VISIBILITY_BUFFERS \
	[[vk::binding(25,1)]] RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT]; \
	[[vk::binding(25+VISIBILITY_BUFFER_COUNT,1)]] RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];
#include "../visibility_buffer.hlsli"

#endif

#endif