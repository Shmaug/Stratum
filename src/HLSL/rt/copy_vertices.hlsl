#pragma compile dxc -spirv -T cs_6_7 -E main

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
void main(uint3 index : SV_DispatchThreadId) {
	if (index.x >= gPushConstants.gCount) return;
	VertexData v;
	float2 uv = gPushConstants.gTexcoordStride > 0 ? asfloat(gTexcoords.Load2(index.x*gPushConstants.gTexcoordStride)) : 0;
	v.mPosition = asfloat(gPositions.Load3(index.x*gPushConstants.gPositionStride));
	v.mU = uv.x;
	v.mNormal = asfloat(gNormals.Load3(index.x*gPushConstants.gNormalStride));
	v.mV = uv.y;
	v.mTangent = gPushConstants.gTangentStride > 0 ? asfloat(gTangents.Load4(index.x*gPushConstants.gTangentStride)) : 0;
	gVertices[index.x] = v;
}