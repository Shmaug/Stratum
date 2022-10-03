#ifndef MSE_H
#define MSE_H

#ifdef __cplusplus
namespace stm {
#endif

enum class CompareMetric {
	eSMAPE,
	eMSE,
	eAverage,
	eCompareMetricCount
};

#ifdef __cplusplus
}
namespace std {
inline string to_string(const stm::CompareMetric& m) {
	switch (m) {
		default: return "Unknown";
		case stm::CompareMetric::eSMAPE: return "SMAPE";
		case stm::CompareMetric::eMSE: return "MSE";
		case stm::CompareMetric::eAverage: return "Average";
	}
};
}
#endif

#endif