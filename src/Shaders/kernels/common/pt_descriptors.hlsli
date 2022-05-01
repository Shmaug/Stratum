#ifndef PT_DESCRIPTORS_H
#define PT_DESCRIPTORS_H

#include <scene.h>

#ifdef PT_DESCRIPTOR_SET_0

[[vk::binding(0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding(2,0)]] ByteAddressBuffer gIndices;
[[vk::binding(3,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding(4,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(4,0)]] StructuredBuffer<InstanceMotionTransform> gInstanceMotionTransforms;
[[vk::binding(5,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding(6,0)]] RWByteAddressBuffer gCounters;
[[vk::binding(7,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(8,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(9,0)]] SamplerState gSampler;
[[vk::binding(10,0)]] Texture2D<float4> gImages[gImageCount];

#endif

#ifdef PT_DESCRIPTOR_SET_1

#include <reservoir.h>
#include <path_tracer.h>

[[vk::binding(0,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(1,1)]] RWStructuredBuffer<PathVertex> gPathStateVertices;
[[vk::binding(2,1)]] RWStructuredBuffer<ShadingData> gPathStateShadingData;
[[vk::binding(3,1)]] RWStructuredBuffer<PathVertex> gLightPathVertices;
[[vk::binding(4,1)]] RWStructuredBuffer<ShadingData> gLightPathShadingData;

[[vk::binding(5,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(6,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(7,1)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(8,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding(9,1)]] RWStructuredBuffer<uint> gRadianceMutex;

[[vk::binding(10,1)]] StructuredBuffer<uint> gViewVolumeInstances;
[[vk::binding(11,1)]] StructuredBuffer<uint> gInstanceIndexMap;
[[vk::binding(19,1)]] SamplerState gSampler1;

#endif

#endif