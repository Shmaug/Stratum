#include "scene.hlsli"

#define SAMPLE_FLAG_BG_IS 1

struct InstanceData {
	TransformData mTransform;
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