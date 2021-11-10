#pragma compile dxc -spirv -T cs_6_7 -E copy

#include "rtscene.hlsli"

[[vk::binding(0,0)]] RWStructuredBuffer<VertexData> gVertices;
[[vk::binding(1,1)]] ByteAddressBuffer gPositions;
[[vk::binding(2,1)]] ByteAddressBuffer gNormals;
[[vk::binding(3,1)]] ByteAddressBuffer gTangents;
[[vk::binding(4,1)]] ByteAddressBuffer gTexcoords;

[[vk::push_constant]] const struct {
	uint gCount;
	uint gPositionStride;
	uint gNormalStride;
	uint gTangentStride;
	uint gTexcoordStride;
} gPushConstants;

[numthreads(32,1,1)]
void copy(uint3 index : SV_DispatchThreadId) {
	if (index.x >= gPushConstants.gCount) return;
	VertexData v;
	float2 uv = asfloat(gTexcoords.Load2(index.x*gPushConstants.gTexcoordStride));
	v.mPosition = asfloat(gPositions.Load3(index.x*gPushConstants.gPositionStride));
	v.mU = uv.x;
	v.mNormal = asfloat(gNormals.Load3(index.x*gPushConstants.gNormalStride));
	v.mV = uv.y;
	v.mTangent = asfloat(gTangents.Load4(index.x*gPushConstants.gTangentStride));
	gVertices[index.x] = v;
}