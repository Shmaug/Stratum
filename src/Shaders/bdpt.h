#ifndef BDPT_H
#define BDPT_H

#ifdef __cplusplus
namespace stm {
#endif

#define BDPT_FLAG_DEMODULATE_ALBEDO			BIT(0)
#define BDPT_FLAG_RAY_CONES 				BIT(1)
#define BDPT_FLAG_HAS_ENVIRONMENT 			BIT(2)
#define BDPT_FLAG_HAS_EMISSIVES 			BIT(3)
#define BDPT_FLAG_HAS_MEDIA 				BIT(4)
#define BDPT_FLAG_NEE 						BIT(5)
#define BDPT_FLAG_MIS 						BIT(6)
#define BDPT_FLAG_UNIFORM_SPHERE_SAMPLING	BIT(7)

#define BDPT_FLAG_COUNT_RAYS				BIT(31)

struct BDPTPushConstants {
	uint gViewCount;
	uint gLightCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;
	uint gRandomSeed;
	uint gMinPathVertices;
	uint gMaxPathVertices;
	uint gMaxNullCollisions;
};

struct PathState {
	float3 position;
	uint path_length;
	float3 dir_out;
	uint medium;
	float3 beta;
	uint packed_pixel;
};

enum class DebugMode {
	eNone,
	eAlbedo,
	eShadingNormal,
	eGeometryNormal,
	eDirOut,
	eDirOutPdf,
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
		case stm::DebugMode::eShadingNormal: return "ShadingNormal";
		case stm::DebugMode::eGeometryNormal: return "GeometryNormal";
		case stm::DebugMode::eDirOut: return "DirOut";
		case stm::DebugMode::eDirOutPdf: return "DirOutPdf";
		case stm::DebugMode::eDebugModeCount: return "DebugModeCount";
	}
};
}
#endif

#endif