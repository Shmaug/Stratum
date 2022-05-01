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
}
#endif

#endif