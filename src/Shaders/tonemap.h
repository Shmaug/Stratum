#ifndef TONEMAP_H
#define TONEMAP_H

#ifdef __cplusplus
namespace stm {
#endif

enum class TonemapMode {
	eRaw,
	eReinhard,
	eReinhardExtended,
	eReinhardLuminance,
	eReinhardLuminanceExtended,
	eUncharted2,
	eFilmic,
	eACES,
	eACESApprox,
	eViridisR,
	eViridisLengthRGB,
	eTonemapModeCount
};

#ifdef __cplusplus

static const unordered_set<TonemapMode> gTonemapModeNeedsMax = {
	TonemapMode::eViridisR,
	TonemapMode::eReinhardExtended,
	TonemapMode::eReinhardLuminanceExtended,
	TonemapMode::eUncharted2
};

}

namespace std {
inline string to_string(const stm::TonemapMode& m) {
	switch (m) {
		default: return "Unknown";
		case stm::TonemapMode::eRaw: return "Raw";
		case stm::TonemapMode::eReinhard: return "Reinhard";
		case stm::TonemapMode::eReinhardExtended: return "Reinhard Extended";
		case stm::TonemapMode::eReinhardLuminance: return "Reinhard (Luminance)";
		case stm::TonemapMode::eReinhardLuminanceExtended: return "Reinhard Extended (Luminance)";
		case stm::TonemapMode::eUncharted2: return "Uncharted 2";
		case stm::TonemapMode::eFilmic: return "Filmic";
		case stm::TonemapMode::eACES: return "ACES";
		case stm::TonemapMode::eACESApprox: return "ACES (approximated)";
		case stm::TonemapMode::eViridisR: return "Viridis Colormap - R channel";
		case stm::TonemapMode::eViridisLengthRGB: return "Viridis Colormap - length(RGB)";
	}
};
}
#endif

#endif