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
	eNormals,
	eAlbedo,
	ePrevUV,
	eAccumLength,
	eAntilag,
	eVariance,
	eDebugModeCount
};

#ifdef __cplusplus
inline string to_string(const TonemapMode& m) {
	switch (m) {
		default:
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
		case DebugMode::eNormals: return "Normals";
		case DebugMode::eAlbedo: return "Albedo";
		case DebugMode::ePrevUV: return "PrevUV";
		case DebugMode::eAccumLength: return "AccumLength";
		case DebugMode::eAntilag: return "Antilag";
		case DebugMode::eVariance: return "Variance";
	}
};
#endif


#endif