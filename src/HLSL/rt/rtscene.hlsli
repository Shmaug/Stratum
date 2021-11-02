#include "../scene.hlsli"

#define SAMPLE_FLAG_BG_IS 1
#define SAMPLE_FLAG_LIGHT_IS 2

#define PROCEDURAL_PRIMITIVE_TYPE_LIGHT 1

struct InstanceData {
	TransformData mTransform;
	TransformData mPrevTransform;
	uint mMaterialIndex;
	uint mFirstVertex;
	uint mIndexByteOffset;
	uint mIndexStride;
};

struct VertexData {
	float4 mPositionU;
	float4 mNormalV;
	float4 mTangent;
};

#ifndef __cplusplus

struct SurfaceData {
	InstanceData instance;
	VertexData v;
	float3 Ng;
	float area;
};

SurfaceData surface_attributes(InstanceData instance, StructuredBuffer<VertexData> vertices, ByteAddressBuffer indices, uint primitiveIndex, float2 bary) {
	SurfaceData sfc;
	sfc.instance = instance;
	// load indices
	uint offsetBytes = sfc.instance.mIndexByteOffset + primitiveIndex*3*(sfc.instance.mIndexStride&0xFF);
	uint3 tri;
	if ((sfc.instance.mIndexStride&0xFF) == 2) {
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
	tri += sfc.instance.mFirstVertex;

	// load vertex data
	sfc.v         = vertices[tri.x];
	VertexData v1 = vertices[tri.y];
	VertexData v2 = vertices[tri.z];

	v1.mPositionU -= sfc.v.mPositionU;
	v2.mPositionU -= sfc.v.mPositionU;
	v1.mNormalV -= sfc.v.mNormalV;
	v2.mNormalV -= sfc.v.mNormalV;
	v1.mTangent -= sfc.v.mTangent;
	v2.mTangent -= sfc.v.mTangent;
	
	sfc.v.mPositionU += v1.mPositionU*bary.x + v2.mPositionU*bary.y;
	sfc.v.mNormalV   += v1.mNormalV*bary.x + v2.mNormalV*bary.y;
	sfc.v.mTangent   += v1.mTangent*bary.x + v2.mTangent*bary.y;
	sfc.Ng = cross(v1.mPositionU.xyz, v2.mPositionU.xyz);

	sfc.v.mPositionU.xyz = transform_point(sfc.instance.mTransform, sfc.v.mPositionU.xyz);
	sfc.v.mNormalV.xyz = normalize(transform_vector(sfc.instance.mTransform, sfc.v.mNormalV.xyz));
	sfc.v.mTangent.xyz = normalize(transform_vector(sfc.instance.mTransform, sfc.v.mTangent.xyz));
	sfc.Ng = transform_vector(sfc.instance.mTransform, sfc.Ng);

	sfc.area = length(sfc.Ng);
	sfc.Ng /= sfc.area;
	sfc.area /= 2;
	return sfc;
}
#endif