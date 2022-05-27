#ifndef BDPT_H
#define BDPT_H

#ifdef __cplusplus
#pragma pack(push)
#pragma pack(1)
namespace stm {
#endif

#define BDPT_FLAG_REMAP_THREADS				BIT(0)
#define BDPT_FLAG_DEMODULATE_ALBEDO			BIT(1)
#define BDPT_FLAG_RAY_CONES 				BIT(2)
#define BDPT_FLAG_HAS_ENVIRONMENT 			BIT(3)
#define BDPT_FLAG_HAS_EMISSIVES 			BIT(4)
#define BDPT_FLAG_HAS_MEDIA 				BIT(5)

#define BDPT_FLAG_NEE 						BIT(6)
#define BDPT_FLAG_NEE_MIS 					BIT(7)
#define BDPT_FLAG_SAMPLE_LIGHT_POWER		BIT(8)
#define BDPT_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(9)

#define BDPT_FLAG_RESERVOIR_NEE				BIT(10)

#define BDPT_FLAG_TRACE_LIGHT				BIT(11)
#define BDPT_FLAG_CONNECT_TO_VIEWS			BIT(12)
#define BDPT_FLAG_CONNECT_TO_LIGHT_PATHS	BIT(13)

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
	uint gDebugViewPathLength;
	uint gDebugLightPathLength;
};

struct PathState {
	float3 position;
	uint medium;
	float3 beta;
	uint packed_pdfs;
	float3 dir_out;
	uint path_length;
#ifdef __HLSL__
	SLANG_MUTATING
	inline void pack_pdfs(const float pdf_fwd, const float pdf_rev) {
		BF_SET(packed_pdfs, asuint(pdf_fwd), 0, 16);
		BF_SET(packed_pdfs, asuint(pdf_rev), 16, 16);
	}
	inline float pdf_fwd() { return f16tof32(packed_pdfs); }
	inline float pdf_rev() { return f16tof32(packed_pdfs>>16); }
#endif
};


#define PATH_VERTEX_FLAG_IS_MATERIAL 	BIT(0)
#define PATH_VERTEX_FLAG_FLIP_BITANGENT	BIT(1)
#define PATH_VERTEX_FLAG_IS_MEDIUM 		BIT(2)
#define PATH_VERTEX_FLAG_IS_ENVIRONMENT	BIT(3)

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
	inline bool is_material() { return material_address_flags & PATH_VERTEX_FLAG_IS_MATERIAL; }
	inline bool is_medium() { return material_address_flags & PATH_VERTEX_FLAG_IS_MEDIUM; }
	inline bool is_environment() { return material_address_flags & PATH_VERTEX_FLAG_IS_ENVIRONMENT; }
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
	eNEEContribution,
	eWeightedNEEContribution,
	eLightTraceContribution,
	ePathLengthContribution,
	eDebugModeCount
};

#ifdef __cplusplus
}
#pragma pack(pop)

namespace std {
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
		case stm::DebugMode::eNEEContribution: return "NEE Contribution";
		case stm::DebugMode::eWeightedNEEContribution: return "Weighted NEE Contribution";
		case stm::DebugMode::eLightTraceContribution: return "Light Trace Contribution";
		case stm::DebugMode::ePathLengthContribution: return "Path Contribution (per length)";
		case stm::DebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif