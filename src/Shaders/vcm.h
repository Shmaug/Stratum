#ifndef VCM_H
#define VCM_H

#ifdef __cplusplus
#pragma pack(push)
#pragma pack(1)
namespace stm {
#endif

#define VCM_FLAG_HAS_ENVIRONMENT 		BIT(0)
#define VCM_FLAG_HAS_EMISSIVES 			BIT(1)
#define VCM_FLAG_HAS_MEDIA 				BIT(2)
#define VCM_FLAG_REMAP_THREADS			BIT(3)
#define VCM_FLAG_USE_ALPHA_TEST			BIT(4)
#define VCM_FLAG_USE_NORMAL_MAPS		BIT(5)
#define VCM_FLAG_USE_VM					BIT(6)
#define VCM_FLAG_USE_VC					BIT(7)
#define VCM_FLAG_USE_PPM				BIT(8)
#define VCM_FLAG_USE_MIS				BIT(9)
#define VCM_FLAG_USE_NEE				BIT(10)
#define VCM_FLAG_LIGHT_TRACE_ONLY		BIT(11)
#define VCM_FLAG_COUNT_RAYS				BIT(31)

struct VCMPushConstants {
	uint2 gOutputExtent;
	uint gViewCount;
	uint gLightCount;
	uint gEnvironmentMaterialAddress;
	uint gRandomSeed;

	uint gMinPathLength;
	uint gMaxPathLength;
	uint gMaxNullCollisions;

	uint gDebugViewPathLength;
	uint gDebugLightPathLength;
};

#define PATH_VERTEX_FLAG_FLIP_BITANGENT	BIT(0)
#define PATH_VERTEX_FLAG_IS_BACKGROUND	BIT(1)
#define PATH_VERTEX_FLAG_IS_MEDIUM 		BIT(2)
#define PATH_VERTEX_FLAG_IS_DELTA	    BIT(3)
#define PATH_VERTEX_FLAG_PREV_IS_DELTA  BIT(4)

struct VcmVertex {
	float3 position;
	uint material_address_flags;

	float2 uv;
	uint2 packed_beta;

	uint packed_geometry_normal;
	uint packed_shading_normal;
	uint packed_tangent;
	uint packed_local_dir_in;

	uint path_length;
	float dVCM; // MIS quantity used for vertex connection and merging
	float dVC;  // MIS quantity used for vertex connection
	float dVM;  // MIS quantity used for vertex merging

#ifdef __HLSL__
	SLANG_MUTATING
	void pack_beta(const float3 beta) {
		packed_beta = uint2(f32tof16(beta[0]) | (f32tof16(beta[1]) << 16), f32tof16(beta[2]) );
	}
	float3 beta() {
		return float3(f16tof32(packed_beta[0]), f16tof32(packed_beta[0] >> 16), f16tof32(packed_beta[1]));
	}
	float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
	bool is_medium() { return material_address_flags & PATH_VERTEX_FLAG_IS_MEDIUM; }
	uint material_address() { return BF_GET(material_address_flags, 4, 28); }
	float3 local_dir_in()   { return unpack_normal_octahedron(packed_local_dir_in); }
#endif
};

enum class VCMDebugMode {
	eNone,
	eAlbedo,
	eEmission,
	eSpecular,
	eShadingNormal,
	eGeometryNormal,
	eDirOut,
	ePrevUV,
	ePathLengthContribution,
	eDebugModeCount
};

#ifdef __cplusplus
}
#pragma pack(pop)

namespace std {
inline string to_string(const stm::VCMDebugMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::VCMDebugMode::eNone: return "None";
		case stm::VCMDebugMode::eAlbedo: return "Albedo";
		case stm::VCMDebugMode::eEmission: return "Emission";
		case stm::VCMDebugMode::eSpecular: return "Specular";
		case stm::VCMDebugMode::eShadingNormal: return "Shading Normal";
		case stm::VCMDebugMode::eGeometryNormal: return "Geometry Normal";
		case stm::VCMDebugMode::eDirOut: return "Bounce Direction";
		case stm::VCMDebugMode::ePrevUV: return "Prev UV";
		case stm::VCMDebugMode::ePathLengthContribution: return "Path Contribution (per length)";
		case stm::VCMDebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif