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

#define BDPT_FLAG_REMAP_THREADS				BIT(8)
#define BDPT_FLAG_DEMODULATE_ALBEDO			BIT(9)
#define BDPT_FLAG_RAY_CONES 				BIT(10)
#define BDPT_FLAG_SAMPLE_BSDFS 				BIT(11)

#define BDPT_FLAG_NEE 						BIT(16)
#define BDPT_FLAG_NEE_MIS 					BIT(17)
#define BDPT_FLAG_SAMPLE_LIGHT_POWER		BIT(18)
#define BDPT_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(19)
#define BDPT_FLAG_PRESAMPLE_LIGHTS			BIT(20)
#define BDPT_FLAG_RESERVOIR_NEE				BIT(21)
#define BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE	BIT(22)
#define BDPT_FLAG_RESERVOIR_SPATIAL_REUSE	BIT(23)
#define BDPT_FLAG_RESERVOIR_UNBIASED_REUSE	BIT(24)

#define BDPT_FLAG_TRACE_LIGHT				BIT(26)
#define BDPT_FLAG_CONNECT_TO_VIEWS			BIT(27)
#define BDPT_FLAG_CONNECT_TO_LIGHT_PATHS	BIT(28)

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
	uint gMaxLightPathVertices;
	uint gMaxNullCollisions;

	uint gLightPathCount;

	uint gNEEReservoirM;
	uint gNEEReservoirSpatialSamples;
	uint gNEEReservoirSpatialRadius;
	uint gReservoirMaxM;
	uint gLightPresampleTileSize;
	uint gLightPresampleTileCount;

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
	float bsdf_pdf;
	float3 dir_in;
	float pad;

	SLANG_MUTATING
	inline void pack_path_length_medium(const uint path_length, const uint medium) {
		BF_SET(path_length_medium, path_length, 0, 16);
		BF_SET(path_length_medium, medium, 16, 16);
	}
	inline uint path_length() CONST_CPP { return BF_GET(path_length_medium,0,16); }
	inline uint medium() CONST_CPP { return BF_GET(path_length_medium,16,16); }
};

struct PresampledLightPoint {
	float3 position;
	uint packed_geometry_normal;
	float3 Le;
	uint pdfA;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
#endif

};

#define PATH_VERTEX_FLAG_IS_MEDIUM 		BIT(0)
#define PATH_VERTEX_FLAG_FLIP_BITANGENT	BIT(1)

struct LightPathVertex0 {
	float3 position;
	uint packed_geometry_normal;
#ifdef __HLSL__
	inline float3 geometry_normal() { return unpack_normal_octahedron(packed_geometry_normal); }
#endif
};

struct LightPathVertex1 {
	uint material_address_flags;
	uint packed_local_dir_in;
	uint packed_shading_normal;
	uint packed_tangent;
#ifdef __HLSL__
	inline bool is_medium() { return material_address_flags & PATH_VERTEX_FLAG_IS_MEDIUM; }
	inline uint material_address() CONST_CPP { return BF_GET(material_address_flags, 4, 28); }

	inline float3 local_dir_in()   { return unpack_normal_octahedron(packed_local_dir_in); }
	inline float3 shading_normal() { return unpack_normal_octahedron(packed_shading_normal); }
	inline float3 tangent()        { return unpack_normal_octahedron(packed_tangent); }
	inline float3 to_world(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return v.x*t + v.y*cross(n, t)*((material_address_flags & PATH_VERTEX_FLAG_FLIP_BITANGENT) ? -1 : 1) + v.z*n;
	}
	inline float3 to_local(const float3 v) {
		const float3 n = shading_normal();
		const float3 t = tangent();
		return float3(dot(v, t), dot(v, cross(n, t)*((material_address_flags & PATH_VERTEX_FLAG_FLIP_BITANGENT) ? -1 : 1)), dot(v, n));
	}
#endif
};

struct LightPathVertex2 {
	float2 uv;
	uint2 packed_beta;
#ifdef __HLSL__
	SLANG_MUTATING
	inline void pack_beta(const float3 beta) {
		packed_beta = uint2(f32tof16(beta[0]) | (f32tof16(beta[1]) << 16), f32tof16(beta[2]));
	}
	inline float3 beta() {
		return float3(f16tof32(packed_beta[0]), f16tof32(packed_beta[0] >> 16), f16tof32(packed_beta[1]));
	}
#endif
};

struct LightPathVertex3 {
	float pdf_fwd;
	float pdf_rev;
};

// to_string of an IntegratorType must be the name of the entry point in the shader
enum class IntegratorType {
	eMultiKernel,
	eSingleKernel,
	eIntegratorTypeCount
};
enum class BDPTDebugMode {
	eNone,
	eAlbedo,
	eDiffuse,
	eSpecular,
	eTransmission,
	eRoughness,
	eEmission,
	eShadingNormal,
	eGeometryNormal,
	eDirOut,
	ePrevUV,
	ePathLengthContribution,
	eReservoirWeight,
	eDebugModeCount
};

#ifdef __cplusplus
}
#pragma pack(pop)

namespace std {
inline string to_string(const stm::IntegratorType& m) {
	switch (m) {
		default: return "Unknown";
		case stm::IntegratorType::eMultiKernel: return "multi_kernel";
		case stm::IntegratorType::eSingleKernel: return "single_kernel";
		case stm::IntegratorType::eIntegratorTypeCount: return "IntegratorTypeCount";
	}
};
inline string to_string(const stm::BDPTDebugMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::BDPTDebugMode::eNone: return "None";
		case stm::BDPTDebugMode::eAlbedo: return "Albedo";
		case stm::BDPTDebugMode::eDiffuse: return "Diffuse";
		case stm::BDPTDebugMode::eSpecular: return "Specular";
		case stm::BDPTDebugMode::eTransmission: return "Transmission";
		case stm::BDPTDebugMode::eRoughness: return "Roughness";
		case stm::BDPTDebugMode::eEmission: return "Emission";
		case stm::BDPTDebugMode::eShadingNormal: return "Shading Normal";
		case stm::BDPTDebugMode::eGeometryNormal: return "Geometry Normal";
		case stm::BDPTDebugMode::eDirOut: return "Bounce Direction";
		case stm::BDPTDebugMode::ePrevUV: return "Prev UV";
		case stm::BDPTDebugMode::ePathLengthContribution: return "Path Contribution (per length)";
		case stm::BDPTDebugMode::eReservoirWeight: return "Reservoir Weight";
		case stm::BDPTDebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif