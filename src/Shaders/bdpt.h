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
#define BDPT_FLAG_RESERVOIR_NEE				BIT(20)
#define BDPT_FLAG_PRESAMPLE_RESERVOIR_NEE	BIT(21)
#define BDPT_FLAG_RESERVOIR_SPATIAL_REUSE	BIT(22)
#define BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE	BIT(23)

#define BDPT_FLAG_TRACE_LIGHT				BIT(24)
#define BDPT_FLAG_CONNECT_TO_VIEWS			BIT(25)
#define BDPT_FLAG_CONNECT_TO_LIGHT_PATHS	BIT(26)

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

	uint gNEEReservoirSamples;
	uint gNEEReservoirSpatialSamples;
	uint gReservoirPresampleTileCount;
	uint gReservoirMaxM;

	uint gDebugViewPathLength;
	uint gDebugLightPathLength;
};

struct PathState {
	float3 position;
	uint path_length_medium;
	float3 beta;
	float pdf_fwd;
	float3 dir_out;
	float pdf_rev;
#ifdef __HLSL__
	SLANG_MUTATING
	inline void pack_path_length_medium(const uint path_length, const uint medium) {
		BF_SET(path_length_medium, path_length, 0, 16);
		BF_SET(path_length_medium, medium, 16, 16);
	}
	inline uint path_length() { return BF_GET(path_length_medium,0,16); }
	inline uint medium() { return BF_GET(path_length_medium,16,16); }
#endif
};

struct PointSample {
	float3 local_position;
	uint instance_primitive_index;
	inline uint instance_index() CONST_CPP { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() CONST_CPP { return BF_GET(instance_primitive_index, 16, 16); }
};

struct PresampledLightPoint {
	PointSample rs;
	float3 local_to_light;
	uint packed_contrib_pdf;
#ifdef __HLSL__
	// luminance(light_radiance) * G
	inline float contribution() { return f16tof32(packed_contrib_pdf); }
	inline float pdfA()         { return f16tof32(packed_contrib_pdf >> 16); }
	SLANG_MUTATING
	inline void pack_contrib_pdf(const float contrib, const float pdf) {
		packed_contrib_pdf = f32tof16(contrib) | (f32tof16(pdf)<<16);
	}
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

	inline float3 local_dir_in()  { return unpack_normal_octahedron2(packed_local_dir_in); }
	inline float3 shading_normal() { return unpack_normal_octahedron(packed_shading_normal); }
	inline float3 tangent() { return unpack_normal_octahedron(packed_tangent); }
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
		packed_beta[0] = f32tof16(beta[0]) | (f32tof16(beta[1]) << 16);
		packed_beta[1] = f32tof16(beta[2]);
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

enum class IntegratorType {
	eNaiveSingleBounce,
	eNaiveMultiBounce,
	eIntegratorTypeCount
};
enum class DebugMode {
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
		case stm::IntegratorType::eNaiveSingleBounce: return "naive_single_bounce";
		case stm::IntegratorType::eNaiveMultiBounce: return "naive_multi_bounce";
		case stm::IntegratorType::eIntegratorTypeCount: return "IntegratorTypeCount";
	}
};
inline string to_string(const stm::DebugMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::DebugMode::eNone: return "None";
		case stm::DebugMode::eAlbedo: return "Albedo";
		case stm::DebugMode::eDiffuse: return "Diffuse";
		case stm::DebugMode::eSpecular: return "Specular";
		case stm::DebugMode::eTransmission: return "Transmission";
		case stm::DebugMode::eRoughness: return "Roughness";
		case stm::DebugMode::eEmission: return "Emission";
		case stm::DebugMode::eShadingNormal: return "Shading Normal";
		case stm::DebugMode::eGeometryNormal: return "Geometry Normal";
		case stm::DebugMode::eDirOut: return "Bounce Direction";
		case stm::DebugMode::ePathLengthContribution: return "Path Contribution (per length)";
		case stm::DebugMode::eReservoirWeight: return "Reservoir Weight";
		case stm::DebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif