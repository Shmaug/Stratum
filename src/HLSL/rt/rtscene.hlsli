#include "../scene.hlsli"
#include "ray_differential.hlsli"

#define SAMPLE_FLAG_BG_IS 1
#define SAMPLE_FLAG_LIGHT_IS 2
#define SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET 24

#define PROCEDURAL_PRIMITIVE_TYPE_LIGHT 1

struct InstanceData {
	TransformData mTransform;
	TransformData mMotionTransform;
	uint mMaterialIndex;
	uint mFirstVertex;
	uint mIndexByteOffset;
	uint mIndexStride;
};

#ifndef __cplusplus

struct SurfaceSample {
	VertexData v;
	float3 Ng;
	float area;
	bool front_face;
	differential3 dN;
	differential2 dUV;
};

void Onb(float3 N, out float3 T, out float3 B) {
    if (N.x != N.y || N.x != N.z)
        T = float3(N.z - N.y, N.x - N.z, N.y - N.x);  //(1,1,1)x N
    else
        T = float3(N.z - N.y, N.x + N.z, -N.y - N.x);  //(-1,1,1)x N
    T = normalize(T);
    B = cross(N, T);
}

uint3 load_tri(InstanceData instance, ByteAddressBuffer indices, uint primitiveIndex) {
	// load indices
	uint offsetBytes = instance.mIndexByteOffset + primitiveIndex*3*(instance.mIndexStride&0xFF);
	uint3 tri;
	if ((instance.mIndexStride&0xFF) == 2) {
		// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl
		uint dwordAlignedOffset = offsetBytes & ~3;    
		uint2 four16BitIndices = indices.Load2(dwordAlignedOffset);
		if (dwordAlignedOffset == offsetBytes) {
				tri.x = four16BitIndices.x & 0xffff;
				tri.y = (four16BitIndices.x >> 16) & 0xffff;
				tri.z = four16BitIndices.y & 0xffff;
		} else {
				tri.x = (four16BitIndices.x >> 16) & 0xffff;
				tri.y = four16BitIndices.y & 0xffff;
				tri.z = (four16BitIndices.y >> 16) & 0xffff;
		}
	} else
		tri = indices.Load3(offsetBytes);
	return tri + instance.mFirstVertex;
}

float3 surface_local_pos(InstanceData instance, StructuredBuffer<VertexData> vertices, ByteAddressBuffer indices, uint primitiveIndex, float2 bary) {
	uint3 tri = load_tri(instance, indices, primitiveIndex);
	// load vertex data
	VertexData v0 = vertices[tri.x];
	VertexData v1 = vertices[tri.y];
	VertexData v2 = vertices[tri.z];
	return v0.mPosition + (v1.mPosition - v0.mPosition)*bary.x + (v2.mPosition - v0.mPosition)*bary.y;
}

SurfaceSample surface_attributes(InstanceData instance, StructuredBuffer<VertexData> vertices, ByteAddressBuffer indices, uint primitiveIndex, float2 bary, float3 P, inout differential3 dP, differential3 dD) {
	SurfaceSample sfc;
	uint3 tri = load_tri(instance, indices, primitiveIndex);
	// load vertex data
	VertexData v0 = vertices[tri.x];
	VertexData v1 = vertices[tri.y];
	VertexData v2 = vertices[tri.z];

	float3 v1v0 = v1.mPosition - v0.mPosition;
	float3 v2v0 = v2.mPosition - v0.mPosition;

	sfc.v.mPosition = transform_point(instance.mTransform, v0.mPosition + v1v0*bary.x + v2v0*bary.y);
	sfc.v.mNormal   = normalize(transform_vector(instance.mTransform, v0.mNormal + (v1.mNormal - v0.mNormal)*bary.x + (v2.mNormal - v0.mNormal)*bary.y));
	sfc.v.mTangent  = v0.mTangent  + (v1.mTangent - v0.mTangent)*bary.x + (v2.mTangent - v0.mTangent)*bary.y;
	sfc.v.mTangent.xyz = normalize(transform_vector(instance.mTransform, sfc.v.mTangent.xyz));
	sfc.v.mU = v0.mU + (v1.mU - v0.mU)*bary.x + (v2.mU - v0.mU)*bary.y;
	sfc.v.mV = v0.mV + (v1.mV - v0.mV)*bary.x + (v2.mV - v0.mV)*bary.y;
	sfc.Ng = transform_vector(instance.mTransform, cross(v1v0, v2v0));
	if (all(sfc.v.mTangent.xyz == 0)) {
		float3 B;
		Onb(sfc.Ng, sfc.v.mTangent.xyz, B);
		sfc.v.mTangent.w = 1;
	}
	sfc.area = length(sfc.Ng);
	sfc.Ng /= sfc.area;
	sfc.area /= 2;
	
	// transfer ray differential
	dP = transfer(sfc.v.mPosition - P, sfc.Ng, P, dP, dD);

	float4 L0 = float4(cross(sfc.Ng, transform_vector(instance.mTransform, v2.mPosition - v1.mPosition)), 0);
	float4 L1 = float4(cross(sfc.Ng, transform_vector(instance.mTransform, v2v0)), 0);
	float4 L2 = float4(cross(sfc.Ng, transform_vector(instance.mTransform, v1v0)), 0);
	L0.w = -dot(L0.xyz, v1.mPosition);
	L1.w = -dot(L1.xyz, v0.mPosition);
	L2.w = -dot(L2.xyz, v0.mPosition);
	L0 /= dot(L0, float4(v0.mPosition,1));
	L1 /= dot(L1, float4(v1.mPosition,1));
	L2 /= dot(L2, float4(v2.mPosition,1));

	float3 dPLx = float3(dot(L0.xyz, dP.dx), dot(L1.xyz, dP.dx), dot(L2.xyz, dP.dx));
	float3 dPLy = float3(dot(L0.xyz, dP.dy), dot(L1.xyz, dP.dy), dot(L2.xyz, dP.dy));

	sfc.dN.dx = dPLx[0]*v0.mNormal + dPLx[1]*v1.mNormal + dPLx[2]*v2.mNormal;
	sfc.dN.dy = dPLy[0]*v0.mNormal + dPLy[1]*v1.mNormal + dPLy[2]*v2.mNormal;
	sfc.dN.dx -= dot(sfc.v.mNormal, sfc.dN.dx)*sfc.v.mNormal;
	sfc.dN.dy -= dot(sfc.v.mNormal, sfc.dN.dy)*sfc.v.mNormal;

	sfc.dUV.dx = dPLx[0]*float2(v0.mU, v0.mV) + dPLx[1]*float2(v1.mU, v1.mV) + dPLx[2]*float2(v2.mU, v2.mV);
	sfc.dUV.dy = dPLy[0]*float2(v0.mU, v0.mV) + dPLy[1]*float2(v1.mU, v1.mV) + dPLy[2]*float2(v2.mU, v2.mV);
	return sfc;
}

#endif