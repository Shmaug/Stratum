#ifndef MSE_H
#define MSE_H

#ifdef __cplusplus
namespace stm {
#endif

enum class ErrorMode {
	eMSELuminance,
	eMSERGB,
	eAverageLuminance,
	eAverageRGB,
	eErrorModeCount
};

#ifdef __cplusplus
}
namespace std {
inline string to_string(const stm::ErrorMode& m) {
	switch (m) {
		default: return "MSE Unknown";
		case stm::ErrorMode::eMSERGB: return "MSE RGB";
		case stm::ErrorMode::eMSELuminance: return "MSE Luminance";
		case stm::ErrorMode::eAverageLuminance: return "Average Luminance";
		case stm::ErrorMode::eAverageRGB: return "Average RGB";
	}
};
}
#endif

#endif