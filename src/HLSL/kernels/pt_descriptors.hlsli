#ifndef PT_DESCRIPTORS_H
#define PT_DESCRIPTORS_H

#include "../scene.hlsli"

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

#include "../reservoir.hlsli"
#include "../path_state.hlsli"

[[vk::binding(0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding(2,1)]] StructuredBuffer<uint> gViewVolumeInstances;
[[vk::binding(3,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding(4,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(5,1)]] RWTexture2D<float4> gPrevRadiance;
[[vk::binding(6,1)]] RWTexture2D<float4> gPrevAlbedo;
[[vk::binding(7,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(8,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(9,1)]] RWStructuredBuffer<PathVertexGeometry> gPathVertices;
[[vk::binding(10,1)]] RWStructuredBuffer<MaterialSampleRecord> gMaterialSamples;
#define DECLARE_VISIBILITY_BUFFERS \
	[[vk::binding(11,1)]] RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT]; \
	[[vk::binding(11+VISIBILITY_BUFFER_COUNT,1)]] RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];
#include "../visibility_buffer.hlsli"

#endif