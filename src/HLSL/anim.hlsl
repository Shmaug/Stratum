#pragma compile glslc -fshader-stage=comp -fentry-point=skin
#pragma compile glslc -fshader-stage=comp -fentry-point=blend

struct VertexWeight {
	float4 weights;
	uint4 indices;
};

[[vk::binding(0)]] RWByteAddressBuffer gVertices;
[[vk::binding(1)]] ByteAddressBuffer gBlendTarget0;
[[vk::binding(2)]] ByteAddressBuffer gBlendTarget1;
[[vk::binding(3)]] ByteAddressBuffer gBlendTarget2;
[[vk::binding(4)]] ByteAddressBuffer gBlendTarget3;
[[vk::binding(5)]] StructuredBuffer<VertexWeight> gWeights;
[[vk::binding(6)]] StructuredBuffer<float4x4> gPose;

[[vk::push_constant]] cbuffer {
	uint gVertexCount;
	uint gVertexStride;
	uint gNormalOffset;
	uint gTangentOffset;
	float4 gBlendFactors;
};

[numthreads(64, 1, 1)]
void skin(uint3 index : SV_DispatchThreadID) {
	if (index.x >= gVertexCount) return;
	
	VertexWeight w = gWeights[index.x];

	float4x4 transform = 0;
	transform += gPose[w.indices[0]] * w.weights[0];
	transform += gPose[w.indices[1]] * w.weights[1];
	transform += gPose[w.indices[2]] * w.weights[2];
	transform += gPose[w.indices[3]] * w.weights[3];

	uint address = index.x * gVertexStride;
	float3 vertex = asfloat(gVertices.Load3(address));
	float3 normal = asfloat(gVertices.Load3(address + gNormalOffset));
	float3 tangent = asfloat(gVertices.Load3(address + gTangentOffset));

	vertex = mul(transform, float4(vertex, 1)).xyz;
	normal = mul((float3x3)transform, normal);
	tangent = mul((float3x3)transform, tangent);

	gVertices.Store3(address, asuint(vertex));
	gVertices.Store3(address + gNormalOffset, asuint(normal));
	gVertices.Store3(address + gTangentOffset, asuint(tangent));
}

[numthreads(64, 1, 1)]
void blend(uint3 index : SV_DispatchThreadID) {
	if (index.x >= gVertexCount) return;
	
	uint address = index.x * gVertexStride;

	float sum = dot(1, abs(gBlendFactors));
	float isum = max(0, 1 - sum);

	float3 vertex  = isum * asfloat(gVertices.Load3(address));
	float3 normal  = isum * asfloat(gVertices.Load3(address + gNormalOffset));
	float3 tangent = isum * asfloat(gVertices.Load3(address + gTangentOffset));

	vertex  += gBlendFactors[0] * asfloat(gBlendTarget0.Load3(address));
	normal  += gBlendFactors[0] * asfloat(gBlendTarget0.Load3(address + gNormalOffset));
	tangent += gBlendFactors[0] * asfloat(gBlendTarget0.Load3(address + gTangentOffset));

	vertex  += gBlendFactors[1] * asfloat(gBlendTarget1.Load3(address));
	normal  += gBlendFactors[1] * asfloat(gBlendTarget1.Load3(address + gNormalOffset));
	tangent += gBlendFactors[1] * asfloat(gBlendTarget1.Load3(address + gTangentOffset));

	vertex  += gBlendFactors[2] * asfloat(gBlendTarget2.Load3(address));
	normal  += gBlendFactors[2] * asfloat(gBlendTarget2.Load3(address + gNormalOffset));
	tangent += gBlendFactors[2] * asfloat(gBlendTarget2.Load3(address + gTangentOffset));

	vertex  += gBlendFactors[3] * asfloat(gBlendTarget3.Load3(address));
	normal  += gBlendFactors[3] * asfloat(gBlendTarget3.Load3(address + gNormalOffset));
	tangent += gBlendFactors[3] * asfloat(gBlendTarget3.Load3(address + gTangentOffset));

	normal = normalize(normal);

	gVertices.Store3(address, asuint(vertex));
	gVertices.Store3(address + gNormalOffset, asuint(normal));
	gVertices.Store3(address + gTangentOffset, asuint(tangent));
}