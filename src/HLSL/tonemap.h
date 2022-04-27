#ifndef TONEMAP_H
#define TONEMAP_H

#ifdef __cplusplus
namespace stm {
#endif

enum class TonemapMode {
	eRaw,
	eReinhard,
	eUncharted2,
	eFilmic,
	eTonemapModeCount
};

enum class DebugMode {
	eNone,
	eZ,
	eDz,
	eShadingNormal,
	eGeometryNormal,
	eTangent,
	eMeanCurvature,
	eRayRadius,
	eUVScreenSize,
	ePrevUV,
	eMaterialID,
	eAlbedo,
	eDemodulatedRadiance,
	eAccumLength,
	eTemporalGradient,
	eRelativeTemporalGradient,
	eLightPathContributions,
	eDebugModeCount
};

#ifdef __cplusplus
}

namespace std {
inline string to_string(const stm::TonemapMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::TonemapMode::eRaw: return "Raw";
		case stm::TonemapMode::eReinhard: return "Reinhard";
		case stm::TonemapMode::eUncharted2: return "Uncharted 2";
		case stm::TonemapMode::eFilmic: return "Filmic";
	}
};
inline string to_string(const stm::DebugMode& m) {
	switch (m) {
		default:
		case stm::DebugMode::eNone: return "None";
		case stm::DebugMode::eZ: return "Z";
		case stm::DebugMode::eDz: return "Dz";
		case stm::DebugMode::eShadingNormal: return "Shading Normal";
		case stm::DebugMode::eGeometryNormal: return "Geometry Normal";
		case stm::DebugMode::eTangent: return "Tangent";
		case stm::DebugMode::eMeanCurvature: return "Mean Curvature";
		case stm::DebugMode::eRayRadius: return "Ray Radius";
		case stm::DebugMode::eUVScreenSize: return "UV Screen Size";
		case stm::DebugMode::ePrevUV: return "PrevUV";
		case stm::DebugMode::eMaterialID: return "Material ID";
		case stm::DebugMode::eAlbedo: return "Albedo";
		case stm::DebugMode::eDemodulatedRadiance: return "Demodulated Radiance";
		case stm::DebugMode::eAccumLength: return "Accum Length";
		case stm::DebugMode::eTemporalGradient: return "Temporal Gradient";
		case stm::DebugMode::eRelativeTemporalGradient: return "Relative Temporal Gradient (Antilag)";
		case stm::DebugMode::eLightPathContributions: return "Light Path Contributions";
	}
};
}
#endif

#endif