#ifndef BDPT_H
#define BDPT_H

#ifdef __cplusplus
#pragma pack(push)
#pragma pack(1)
namespace stm {
#endif

#include "reservoir.h"

enum BDPTFlagBits {
	ePerformanceCounters,
	eRemapThreads,
	eCoherentRR,
	eCoherentSampling,
	eFlipTriangleUVs,
	eFlipNormalMaps,
	eAlphaTest,
	eNormalMaps,
	eShadingNormalShadowFix,
	eRayCones,
	eSampleBSDFs,
	eNEE,
	eMIS,
	eSampleLightPower,
	eUniformSphereSampling,
	ePresampleLights,
	eDeferShadowRays,
	eConnectToViews,
	eConnectToLightPaths,
	eLVC,
	eReservoirs,
	eReservoirReuse,
	eHashGridJitter,
	eSampleEnvironmentMapDirectly,
	eBDPTFlagCount,
};

#define BDPT_CHECK_FLAG(mask, flag) (mask & BIT((uint)flag))
#define BDPT_SET_FLAG(mask, flag) mask |= BIT((uint)flag)
#define BDPT_UNSET_FLAG(mask, flag) mask &= ~BIT((uint)flag)

#define BDPT_FLAG_HAS_ENVIRONMENT 			BIT(0)
#define BDPT_FLAG_HAS_EMISSIVES 			BIT(1)
#define BDPT_FLAG_HAS_MEDIA 				BIT(2)
#define BDPT_FLAG_TRACE_LIGHT				BIT(3)

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
	uint gMaxDiffuseVertices;
	uint gMaxNullCollisions;

	uint gLightPresampleTileSize;
	uint gLightPresampleTileCount;

	uint gLightPathCount;

	uint gReservoirM;
	uint gReservoirMaxM;
	uint gReservoirSpatialM;

	uint gHashGridBucketCount;
	float gHashGridMinBucketRadius;
	float gHashGridBucketPixelRadius;

	uint gDebugViewPathLength;
	uint gDebugLightPathLength;
};

struct ShadowRayData {
	float3 contribution;
	uint rng_offset;
	float3 ray_origin;
	uint medium;
	float3 ray_direction;
	float ray_distance;
};

struct PresampledLightPoint {
	float3 position;
	uint packed_geometry_normal;
	float3 Le;
	float pdfA; // negative for environment map samples
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
#endif
};

#define PATH_VERTEX_FLAG_FLIP_BITANGENT	BIT(0)
#define PATH_VERTEX_FLAG_IS_BACKGROUND	BIT(1)
#define PATH_VERTEX_FLAG_IS_MEDIUM 		BIT(2)
#define PATH_VERTEX_FLAG_IS_PREV_DELTA  BIT(3)

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
	inline void pack_beta(const float3 beta, const uint subpath_length, const uint diffuse_vertices, const uint flags) {
		BF_SET(packed_beta[0], f32tof16(beta[0])  , 0 , 16);
		BF_SET(packed_beta[0], f32tof16(beta[1])  , 16, 16);
		BF_SET(packed_beta[1], f32tof16(beta[2])  , 0 , 16);
		BF_SET(packed_beta[1], subpath_length     , 16, 7);
		BF_SET(packed_beta[1], diffuse_vertices   , 23, 5);
		BF_SET(packed_beta[1], flags              , 28, 4);
	}
	inline float3 beta() CONST_CPP {
		return float3(f16tof32(packed_beta[0]), f16tof32(packed_beta[0] >> 16), f16tof32(packed_beta[1]));
	}
	inline uint subpath_length()   { return BF_GET(packed_beta[1], 16, 7); }
	inline uint diffuse_vertices() { return BF_GET(packed_beta[1], 23, 5); }
	inline bool is_background()    { return BF_GET(packed_beta[1], 28, 4) & PATH_VERTEX_FLAG_IS_BACKGROUND; }
	inline bool is_medium()        { return BF_GET(packed_beta[1], 28, 4) & PATH_VERTEX_FLAG_IS_MEDIUM; }
	inline bool is_prev_delta()    { return BF_GET(packed_beta[1], 28, 4) & PATH_VERTEX_FLAG_IS_PREV_DELTA; }
	inline bool flip_bitangent()   { return BF_GET(packed_beta[1], 28, 4) & PATH_VERTEX_FLAG_FLIP_BITANGENT; }

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

struct ReservoirData {
	Reservoir r;
	uint packed_geometry_normal;
	float W;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
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
inline string to_string(const stm::BDPTFlagBits& m) {
	switch (m) {
		default: return "Unknown";
		case stm::BDPTFlagBits::ePerformanceCounters: return "Performance counters";
		case stm::BDPTFlagBits::eRemapThreads: return "Remap threads";
		case stm::BDPTFlagBits::eCoherentRR: return "Coherent RR";
		case stm::BDPTFlagBits::eCoherentSampling: return "Coherent sampling";
		case stm::BDPTFlagBits::eFlipTriangleUVs: return "Flip triangle UVs";
		case stm::BDPTFlagBits::eFlipNormalMaps: return "Flip normal maps";
		case stm::BDPTFlagBits::eAlphaTest: return "Alpha test";
		case stm::BDPTFlagBits::eNormalMaps: return "Normal maps";
		case stm::BDPTFlagBits::eShadingNormalShadowFix: return "Shading normal shadow fix";
		case stm::BDPTFlagBits::eRayCones: return "Ray cones";
		case stm::BDPTFlagBits::eSampleBSDFs: return "Sample BSDFs";
		case stm::BDPTFlagBits::eNEE: return "NEE";
		case stm::BDPTFlagBits::eMIS: return "MIS";
		case stm::BDPTFlagBits::eSampleLightPower: return "Sample light power";
		case stm::BDPTFlagBits::eUniformSphereSampling: return "Uniform sphere sampling";
		case stm::BDPTFlagBits::ePresampleLights: return "Presample lights";
		case stm::BDPTFlagBits::eDeferShadowRays: return "Defer shadow rays";
		case stm::BDPTFlagBits::eConnectToViews: return "Connect to views";
		case stm::BDPTFlagBits::eConnectToLightPaths: return "Connect to light paths";
		case stm::BDPTFlagBits::eLVC: return "Light vertex cache";
		case stm::BDPTFlagBits::eReservoirs: return "Reservoirs";
		case stm::BDPTFlagBits::eReservoirReuse: return "Reservoir reuse";
		case stm::BDPTFlagBits::eHashGridJitter: return "Jitter hash grid lookups ";
		case stm::BDPTFlagBits::eSampleEnvironmentMapDirectly: return "Sample environment map directly";
	}
}
inline string to_string(const stm::BDPTDebugMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::BDPTDebugMode::eNone: return "None";
		case stm::BDPTDebugMode::eAlbedo: return "Albedo";
		case stm::BDPTDebugMode::eSpecular: return "Specular";
		case stm::BDPTDebugMode::eEmission: return "Emission";
		case stm::BDPTDebugMode::eShadingNormal: return "Shading normal";
		case stm::BDPTDebugMode::eGeometryNormal: return "Geometry normal";
		case stm::BDPTDebugMode::eDirOut: return "Bounce direction";
		case stm::BDPTDebugMode::ePrevUV: return "Prev UV";
		case stm::BDPTDebugMode::eEnvironmentSampleTest: return "Environment map sampling test";
		case stm::BDPTDebugMode::eEnvironmentSamplePDF: return "Environment map sampling PDF";
		case stm::BDPTDebugMode::eReservoirWeight: return "Reservoir weight";
		case stm::BDPTDebugMode::ePathLengthContribution: return "Path contribution (per length)";
		case stm::BDPTDebugMode::eLightTraceContribution: return "Light trace contribution";
		case stm::BDPTDebugMode::eViewTraceContribution: return "View trace contribution";
	}
};
}
#endif

#endif