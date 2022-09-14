#ifndef BDPT_H
#define BDPT_H

#ifdef __cplusplus
#pragma pack(push)
#pragma pack(1)
namespace stm {
#endif

#include "reservoir.h"

#define BDPT_FLAG_HAS_ENVIRONMENT 			BIT(0)
#define BDPT_FLAG_HAS_EMISSIVES 			BIT(1)
#define BDPT_FLAG_HAS_MEDIA 				BIT(2)

#define BDPT_FLAG_REMAP_THREADS				BIT(3)
#define BDPT_FLAG_COHERENT_RR 				BIT(4)
#define BDPT_FLAG_COHERENT_RNG 				BIT(5)

#define BDPT_FLAG_ALPHA_TEST 				BIT(6)
#define BDPT_FLAG_NORMAL_MAPS 				BIT(7)
#define BDPT_FLAG_RAY_CONES 				BIT(8)
#define BDPT_FLAG_RAY_DIFFERENTIAL			BIT(9)
#define BDPT_FLAG_SAMPLE_BSDFS 				BIT(10)
#define BDPT_FLAG_SAMPLE_LIGHT_POWER		BIT(11)
#define BDPT_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(12)
#define BDPT_FLAG_MIS 						BIT(13)

#define BDPT_FLAG_NEE 						BIT(14)
#define BDPT_FLAG_PRESAMPLE_LIGHTS			BIT(15)
#define BDPT_FLAG_RESERVOIR_NEE				BIT(16)
#define BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE	BIT(17)
#define BDPT_FLAG_RESERVOIR_SPATIAL_REUSE	BIT(18)
#define BDPT_FLAG_RESERVOIR_UNBIASED_REUSE	BIT(19)
#define BDPT_FLAG_DEFER_NEE_RAYS			BIT(20)

#define BDPT_FLAG_TRACE_LIGHT				BIT(21)
#define BDPT_FLAG_CONNECT_TO_VIEWS			BIT(22)
#define BDPT_FLAG_CONNECT_TO_LIGHT_PATHS	BIT(23)
#define BDPT_FLAG_LIGHT_TRACE_USE_Z			BIT(24)
#define BDPT_FLAG_DEFER_CONNECTIONS			BIT(25)
#define BDPT_FLAG_LIGHT_VERTEX_CACHE		BIT(26)
#define BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS	BIT(27)

#define BDPT_FLAG_SAMPLE_ENV_TEXTURE		BIT(30)

#define BDPT_FLAG_COUNT_RAYS				BIT(31)

struct BDPTPushConstants {
	uint2 gOutputExtent;
	uint gViewCount;
	uint gLightCount;
	uint gLightDistributionPDF;
	uint gLightDistributionCDF;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;
	uint gRandomSeed;

	uint gMinPathVertices;
	uint gMaxPathVertices;
	uint gMaxNullCollisions;

	uint gLightPresampleTileSize;
	uint gLightPresampleTileCount;

	uint gNEEReservoirM;
	uint gReservoirMaxM;
	uint gNEEReservoirSpatialSamples;
	uint gNEEReservoirSpatialRadius;

	uint gLightPathCount;

	uint gLightVertexReservoirM;

	uint gDebugViewPathLength;
	uint gDebugLightPathLength;
};

struct PointSample {
	float3 local_position;
	uint instance_primitive_index;
	inline uint instance_index() CONST_CPP { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() CONST_CPP { return BF_GET(instance_primitive_index, 16, 16); }
};

struct PathState {
	PointSample p;
	float3 origin;
	uint path_length_medium;
	float3 beta;
	float prev_cos_out;
	float3 dir_in;
	float bsdf_pdf;

	SLANG_MUTATING
	inline void pack_path_length_medium(const uint path_length, const uint medium) {
		BF_SET(path_length_medium, path_length, 0, 16);
		BF_SET(path_length_medium, medium, 16, 16);
	}
	inline uint path_length() CONST_CPP { return BF_GET(path_length_medium,0,16); }
	inline uint medium() CONST_CPP { return BF_GET(path_length_medium,16,16); }
};
struct PathState1 {
	float d;
};

struct PresampledLightPoint {
	float3 position;
	uint packed_geometry_normal;
	float3 Le;
	float pdfA;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
#endif
};

struct NEERayData {
	float3 contribution;
	uint rng_offset;
	float3 ray_origin;
	uint medium;
	float3 ray_direction;
	float dist;
};

#define PATH_VERTEX_FLAG_FLIP_BITANGENT	BIT(0)
#define PATH_VERTEX_FLAG_IS_BACKGROUND	BIT(1)
#define PATH_VERTEX_FLAG_IS_MEDIUM 		BIT(2)
#define PATH_VERTEX_FLAG_IS_DELTA	    BIT(3)
#define PATH_VERTEX_FLAG_PREV_IS_DELTA  BIT(4)

struct PathVertex {
	float3 position;
	uint packed_geometry_normal;
	uint material_address;
	uint packed_local_dir_in;
	uint packed_shading_normal;
	uint packed_tangent;
	float2 uv;
	uint2 packed_beta;
	float prev_dVC; // dL at previous light vertex (dL_{s+2})
	float G_rev; // prev_cos_out/dist2_prev
	float prev_pdfA_fwd; // P(s+1 <- s+2)
	float path_pdf;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
	SLANG_MUTATING
	inline void pack_beta(const float3 beta, const uint subpath_length, const uint flags) {
		BF_SET(packed_beta[0], f32tof16(beta[0])  , 0 , 16);
		BF_SET(packed_beta[0], f32tof16(beta[1])  , 16, 16);
		BF_SET(packed_beta[1], f32tof16(beta[2])  , 0 , 16);
		BF_SET(packed_beta[1], subpath_length     , 16, 11);
		BF_SET(packed_beta[1], flags              , 27, 5);
	}
	inline float3 beta() CONST_CPP {
		return float3(f16tof32(packed_beta[0]), f16tof32(packed_beta[0] >> 16), f16tof32(packed_beta[1]));
	}
	inline uint subpath_length()   { return BF_GET(packed_beta[1], 16, 11); }
	inline bool is_background()    { return BF_GET(packed_beta[1], 27, 5) & PATH_VERTEX_FLAG_IS_BACKGROUND; }
	inline bool is_medium()        { return BF_GET(packed_beta[1], 27, 5) & PATH_VERTEX_FLAG_IS_MEDIUM; }
	inline bool is_delta()         { return BF_GET(packed_beta[1], 27, 5) & PATH_VERTEX_FLAG_IS_DELTA; }
	inline bool prev_is_delta()    { return BF_GET(packed_beta[1], 27, 5) & PATH_VERTEX_FLAG_PREV_IS_DELTA; }
	inline bool flip_bitangent()   { return BF_GET(packed_beta[1], 27, 5) & PATH_VERTEX_FLAG_FLIP_BITANGENT; }

	inline float3 local_dir_in()   { return unpack_normal_octahedron(packed_local_dir_in); }
	inline float3 shading_normal() { return unpack_normal_octahedron(packed_shading_normal); }
	inline float3 tangent()        { return unpack_normal_octahedron(packed_tangent); }
	inline float3 to_world(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return v.x*t + v.y*cross(n, t)*(flip_bitangent() ? -1 : 1) + v.z*n;
	}
	inline float3 to_local(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return float3(dot(v, t), dot(v, cross(n, t)*(flip_bitangent() ? -1 : 1)), dot(v, n));
	}
#endif
};

enum class BDPTDebugMode {
	eNone,
	eAlbedo,
	eSpecular,
	eEmission,
	eShadingNormal,
	eGeometryNormal,
	eDirOut,
	ePrevUV,
	eEnvironmentSampleTest,
	eEnvironmentSamplePDF,
	eReservoirWeight,
	ePathLengthContribution,
	eLightTraceContribution,
	eViewTraceContribution,
	eDebugModeCount
};

#ifdef __cplusplus
}
#pragma pack(pop)

namespace std {
inline string to_string(const stm::BDPTDebugMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::BDPTDebugMode::eNone: return "None";
		case stm::BDPTDebugMode::eAlbedo: return "Albedo";
		case stm::BDPTDebugMode::eSpecular: return "Specular";
		case stm::BDPTDebugMode::eEmission: return "Emission";
		case stm::BDPTDebugMode::eShadingNormal: return "Shading Normal";
		case stm::BDPTDebugMode::eGeometryNormal: return "Geometry Normal";
		case stm::BDPTDebugMode::eDirOut: return "Bounce Direction";
		case stm::BDPTDebugMode::ePrevUV: return "Prev UV";
		case stm::BDPTDebugMode::eEnvironmentSampleTest: return "Environment Map Sampling Test";
		case stm::BDPTDebugMode::eEnvironmentSamplePDF: return "Environment Map Sampling PDF";
		case stm::BDPTDebugMode::eReservoirWeight: return "Reservoir Weight";
		case stm::BDPTDebugMode::ePathLengthContribution: return "Path Contribution (per length)";
		case stm::BDPTDebugMode::eLightTraceContribution: return "Light Trace Contribution";
		case stm::BDPTDebugMode::eViewTraceContribution: return "View Trace Contribution";
		case stm::BDPTDebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif