#ifndef TONEMAP_H
#define TONEMAP_H

enum TonemapMode {
	eRaw,
	eReinhard,
	eUncharted2,
	eFilmic,
	eTonemapModeCount
};

enum DebugMode {
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
inline string to_string(const TonemapMode& m) {
	switch (m) {
		default: return "Unknown";
		case TonemapMode::eRaw: return "Raw";
		case TonemapMode::eReinhard: return "Reinhard";
		case TonemapMode::eUncharted2: return "Uncharted 2";
		case TonemapMode::eFilmic: return "Filmic";
	}
};
inline string to_string(const DebugMode& m) {
	switch (m) {
		default:
		case DebugMode::eNone: return "None";
		case DebugMode::eZ: return "Z";
		case DebugMode::eDz: return "Dz";
		case DebugMode::eShadingNormal: return "Shading Normal";
		case DebugMode::eGeometryNormal: return "Geometry Normal";
		case DebugMode::eTangent: return "Tangent";
		case DebugMode::eMeanCurvature: return "Mean Curvature";
		case DebugMode::eRayRadius: return "Ray Radius";
		case DebugMode::eUVScreenSize: return "UV Screen Size";
		case DebugMode::ePrevUV: return "PrevUV";
		case DebugMode::eMaterialID: return "Material ID";
		case DebugMode::eAlbedo: return "Albedo";
		case DebugMode::eDemodulatedRadiance: return "Demodulated Radiance";
		case DebugMode::eAccumLength: return "Accum Length";
		case DebugMode::eTemporalGradient: return "Temporal Gradient";
		case DebugMode::eRelativeTemporalGradient: return "Relative Temporal Gradient (Antilag)";
		case DebugMode::eLightPathContributions: return "Light Path Contributions";
	}
};
#endif


#endif