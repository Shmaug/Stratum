#pragma kernel CopyIndices
#pragma kernel CopyVertices
#pragma kernel BuildBvh

#include "rtcommon.h"

[[vk::binding(0, 0)]] RWStructuredBuffer<BvhNode> SceneBvh	: register(u0);
[[vk::binding(1, 0)]] RWByteAddressBuffer Vertices				  : register(u1);
[[vk::binding(2, 0)]] RWByteAddressBuffer RWTriangles	      : register(u2);
[[vk::binding(3, 0)]] ByteAddressBuffer Triangles						: register(t0);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> PrimitiveMaterials 	: register(u3);
[[vk::binding(5, 0)]] RWStructuredBuffer<RTLight> Lights						: register(u4);
[[vk::binding(6, 0)]] StructuredBuffer<RTMaterial> Materials				: register(u5);

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
  float4x4 Transform;
	uint VertexStride;
	uint SrcIndexStride;
	uint IndexOffset;
	uint SrcOffset;
	uint DstOffset;

	uint PrimitiveCount;
  uint LeafSize;
};

[numthreads(64, 1, 1)]
void CopyIndices(uint3 index : SV_DispatchThreadID) {
  if (index.x >= PrimitiveCount) return;

  uint i = Triangles.Load((SrcOffset + index.x) * SrcIndexStride);
  if (SrcIndexStride == 2) i = (i & 0xFFFF0000) >> 16u;

  RWTriangles.Store(4 * (DstOffset + index.x), i + IndexOffset);
}

[numthreads(64, 1, 1)]
void CopyVertices(uint3 index : SV_DispatchThreadID) {
  if (index.x >= PrimitiveCount) return;

  uint addr = (index.x + SrcOffset) * VertexStride;
  float3 v = asfloat(Vertices.Load3(addr));
  float3 n = asfloat(Vertices.Load3(addr+12));
  float4 t = asfloat(Vertices.Load4(addr+24));

  v = mul(Transform, float4(v, 1)).xyz;
  n = mul(Transform, float4(n, 0)).xyz;
  t.xyz = mul(Transform, float4(t.xyz, 0)).xyz;

  Vertices.Store3(addr, asuint(v));
  Vertices.Store3(addr+12, asuint(n));
  Vertices.Store4(addr+24, asuint(t));
}

void GetAABB(uint primIndex, out float3 mn, out float3 mx) {
  uint3 addr = VertexStride * Triangles.Load3(3 * 4 * primIndex);
  float3 v0 = asfloat(Vertices.Load3(addr.x));
  float3 v1 = asfloat(Vertices.Load3(addr.y));
  float3 v2 = asfloat(Vertices.Load3(addr.z));
  mn = min(min(v0, v1), v2) - 0.0001;
  mx = max(max(v0, v1), v2) + 0.0001;
}

[numthreads(1, 1, 1)]
void BuildBvh(uint3 index : SV_DispatchThreadID) {
	#define UNTOUCHED 0xffffffff
	#define TOUCHED_TWICE 0xfffffffd

  uint3 todo[128];
	uint stackptr = 0;
	uint nNodes = 0;
	uint nLeafs = 0;
	float3 minCenter;
	float3 maxCenter;
	BvhNode node;

  todo[stackptr].x = 0;               // Start
  todo[stackptr].y = PrimitiveCount;  // End
  todo[stackptr].z = 0xfffffffc;      // ParentOffset
  stackptr++;

	while (stackptr > 0) {
		// Pop the next item off of the stack
    uint3 bnode = todo[--stackptr];
    uint start = bnode.x;
    uint end = bnode.y;
    uint nPrims = end - start;
    node.StartIndex = start;
    node.PrimitiveCount = nPrims;
    node.RightOffset = UNTOUCHED;
      
    GetAABB(start, node.Min, node.Max);
    minCenter = maxCenter = (node.Min + node.Max) / 2;
		for (uint p = start + 1; p < end; p++) {
      float3 pmin, pmax;
      GetAABB(p, pmin, pmax);
      node.Min = min(node.Min, pmin);
      node.Max = max(node.Max, pmax);
      float3 c = (pmin + pmax) / 2;
      minCenter = min(minCenter, c);
      maxCenter = max(maxCenter, c);
		}

		if (nPrims <= LeafSize) {
			node.RightOffset = 0;
			nLeafs++;
		}

		SceneBvh[nNodes] = node;
		nNodes++;

		if (bnode.z != 0xfffffffc) {
			SceneBvh[bnode.z].RightOffset--;
			if (SceneBvh[bnode.z].RightOffset == TOUCHED_TWICE)
				SceneBvh[bnode.z].RightOffset = nNodes - 1 - bnode.z;
		}

		if (node.RightOffset == 0) continue;

		uint splitDim = 0;
		float3 extent = maxCenter - minCenter;
    if (extent.y > extent[splitDim]) splitDim = 1;
    if (extent.z > extent[splitDim]) splitDim = 2;

		float split_coord = (maxCenter + minCenter)[splitDim];
		uint mid = start;
		for (uint i = start; i < end; i++) {
      float3 mini, maxi;
      GetAABB(i, mini, maxi);

			if ((mini + maxi)[splitDim] < split_coord && mid != i) {
        uint3 tri1 = Triangles.Load3(4 * 3 * i);
        uint3 tri2 = Triangles.Load3(4 * 3 * mid);
        Triangles.Store3(4 * 3 * i,   tri2);
        Triangles.Store3(4 * 3 * mid, tri1);
        
        uint m1 = PrimitiveMaterials[i];
        uint m2 = PrimitiveMaterials[mid];
        PrimitiveMaterials[i]   = m2;
        PrimitiveMaterials[mid] = m1;
        AllMemoryBarrier();
        
        mid++;
			}
    }

    if (mid == start || mid == end)
      mid = start + (end - start) / 2;

    todo[stackptr].x = mid;
    todo[stackptr].y = end;
    todo[stackptr].z = nNodes - 1;
    stackptr++;
    todo[stackptr].x = start;
    todo[stackptr].y = mid;
    todo[stackptr].z = nNodes - 1;
    stackptr++;
  }

  uint lightCount = 0;
  for (uint i = 0; i < PrimitiveCount; i++) {
    uint materialIndex = PrimitiveMaterials[i];
    RTMaterial mat = Materials[materialIndex];
    if (any(mat.Emission > 0)){
      Lights[lightCount].PrimitiveIndex = i;
      Lights[lightCount].MaterialIndex = materialIndex;
      lightCount++;
    }
  }
}
