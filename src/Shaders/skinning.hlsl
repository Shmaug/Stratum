#pragma compile compute skin_vertices
#pragma compile compute blend_vertices

struct VertexWeight {
	float4 Weights;
	uint4 Indices;
};

RWByteAddressBuffer Vertices : register(u0);
RWByteAddressBuffer BlendTarget0 : register(u1);
RWByteAddressBuffer BlendTarget1 : register(u2);
RWByteAddressBuffer BlendTarget2 : register(u3);
RWByteAddressBuffer BlendTarget3 : register(u4);
RWStructuredBuffer<VertexWeight> Weights : register(u5);
RWStructuredBuffer<float4x4> Pose : register(u6);

struct PushConstants {
	uint VertexCount;
	uint VertexStride;
	uint NormalOffset;
	uint TangentOffset;
	float4 BlendFactors;
};
[[vk::push_constant]] const PushConstants gPushConstants = { 0, 0, 0, 0, float4(0,0,0,0) };

[numthreads(64, 1, 1)]
void skin_vertices(uint3 index : SV_DispatchThreadID) {
	if (index.x >= gPushConstants.VertexCount) return;
	
	VertexWeight w = Weights[index.x];

	float4x4 transform = 0;
	transform += Pose[w.Indices[0]] * w.Weights[0];
	transform += Pose[w.Indices[1]] * w.Weights[1];
	transform += Pose[w.Indices[2]] * w.Weights[2];
	transform += Pose[w.Indices[3]] * w.Weights[3];

	uint address = index.x * gPushConstants.VertexStride;
	float3 vertex = asfloat(Vertices.Load3(address));
	float3 normal = asfloat(Vertices.Load3(address + gPushConstants.NormalOffset));
	float3 tangent = asfloat(Vertices.Load3(address + gPushConstants.TangentOffset));

	vertex = mul(transform, float4(vertex, 1)).xyz;
	normal = mul((float3x3)transform, normal);
	tangent = mul((float3x3)transform, tangent);

	Vertices.Store3(address, asuint(vertex));
	Vertices.Store3(address + gPushConstants.NormalOffset, asuint(normal));
	Vertices.Store3(address + gPushConstants.TangentOffset, asuint(tangent));
}

[numthreads(64, 1, 1)]
void blend_vertices(uint3 index : SV_DispatchThreadID) {
	if (index.x >= gPushConstants.VertexCount) return;
	
	uint address = index.x * gPushConstants.VertexStride;

	float sum = dot(1, abs(gPushConstants.BlendFactors));
	float isum = max(0, 1 - sum);

	float3 vertex = isum * asfloat(Vertices.Load3(address));
	float3 normal = isum * asfloat(Vertices.Load3(address + gPushConstants.NormalOffset));
	float3 tangent = isum * asfloat(Vertices.Load3(address + gPushConstants.TangentOffset));

	vertex += gPushConstants.BlendFactors[0] * asfloat(BlendTarget0.Load3(address));
	normal += gPushConstants.BlendFactors[0] * asfloat(BlendTarget0.Load3(address + gPushConstants.NormalOffset));
	tangent += gPushConstants.BlendFactors[0] * asfloat(BlendTarget0.Load3(address + gPushConstants.TangentOffset));

	vertex += gPushConstants.BlendFactors[1] * asfloat(BlendTarget1.Load3(address));
	normal += gPushConstants.BlendFactors[1] * asfloat(BlendTarget1.Load3(address + gPushConstants.NormalOffset));
	tangent += gPushConstants.BlendFactors[1] * asfloat(BlendTarget1.Load3(address + gPushConstants.TangentOffset));

	vertex += gPushConstants.BlendFactors[2] * asfloat(BlendTarget2.Load3(address));
	normal += gPushConstants.BlendFactors[2] * asfloat(BlendTarget2.Load3(address + gPushConstants.NormalOffset));
	tangent += gPushConstants.BlendFactors[2] * asfloat(BlendTarget2.Load3(address + gPushConstants.TangentOffset));

	vertex += gPushConstants.BlendFactors[3] * asfloat(BlendTarget3.Load3(address));
	normal += gPushConstants.BlendFactors[3] * asfloat(BlendTarget3.Load3(address + gPushConstants.NormalOffset));
	tangent += gPushConstants.BlendFactors[3] * asfloat(BlendTarget3.Load3(address + gPushConstants.TangentOffset));

	normal = normalize(normal);

	Vertices.Store3(address, asuint(vertex));
	Vertices.Store3(address + gPushConstants.NormalOffset, asuint(normal));
	Vertices.Store3(address + gPushConstants.TangentOffset, asuint(tangent));
}