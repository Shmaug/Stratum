#ifndef BDPT_H
#define BDPT_H

#ifdef __cplusplus
namespace stm {
#endif

#define BDPT_FLAG_REMAP_THREADS				BIT(0)
#define BDPT_FLAG_DEMODULATE_ALBEDO			BIT(1)
#define BDPT_FLAG_RAY_CONES 				BIT(2)
#define BDPT_FLAG_HAS_ENVIRONMENT 			BIT(3)
#define BDPT_FLAG_HAS_EMISSIVES 			BIT(4)
#define BDPT_FLAG_HAS_MEDIA 				BIT(5)
#define BDPT_FLAG_NEE 						BIT(6)
#define BDPT_FLAG_MIS 						BIT(7)
#define BDPT_FLAG_SAMPLE_LIGHT_POWER		BIT(8)
#define BDPT_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(9)

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
};

struct PathState {
	float3 position;
	uint medium;
	float3 beta;
	float pdf_fwd;
	float3 dir_out;
	uint path_length;
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
	eNEEContrib,
	eDebugModeCount
};

#ifdef __cplusplus
}

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
		case stm::DebugMode::eShadingNormal: return "ShadingNormal";
		case stm::DebugMode::eGeometryNormal: return "GeometryNormal";
		case stm::DebugMode::eDirOut: return "DirOut";
		case stm::DebugMode::eNEEContrib: return "NEEContrib";
		case stm::DebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif