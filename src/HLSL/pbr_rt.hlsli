#include "scene.hlsli"

#define DEBUG_NONE 0
#define DEBUG_ALBEDO 1
#define DEBUG_METALLIC 2
#define DEBUG_ROUGHNESS 3
#define DEBUG_SMOOTH_NORMALS 4
#define DEBUG_GEOMETRY_NORMALS 5
#define DEBUG_EMISSION 6
#define DEBUG_PDF 7

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