#if 0
#pragma compile dxc -spirv -T cs_6_7 -E main
#endif

#include "../scene.h"

RWStructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gPositions;
ByteAddressBuffer gNormals;
ByteAddressBuffer gTangents;
ByteAddressBuffer gTexcoords;

struct PushConstants {
	uint gCount;
	uint gPositionStride;
	uint gNormalStride;
	uint gTangentStride;
	uint gTexcoordStride;
};

#ifdef __SLANG__
[[vk::push_constant]] ConstantBuffer<PushConstants> gPushConstants;
#else
[[vk::push_constant]] static const PushConstants gPushConstants;
#endif

SLANG_SHADER("compute")
[numthreads(64,1,1)]
void main(uint3 index : SV_DispatchThreadId) {
	if (index.x >= gPushConstants.gCount) return;
	PackedVertexData v;
	float2 uv = gPushConstants.gTexcoordStride > 0 ? gTexcoords.Load<float2>(index.x*gPushConstants.gTexcoordStride) : 0;
	v.position = gPositions.Load<float3>(index.x*gPushConstants.gPositionStride);
	v.u = uv.x;
	v.normal = gNormals.Load<float3>(index.x*gPushConstants.gNormalStride);
	v.v = uv.y;
	v.tangent = gPushConstants.gTangentStride > 0 ? gTangents.Load<float4>(index.x*gPushConstants.gTangentStride) : 0;
	gVertices[index.x] = v;
}