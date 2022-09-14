#ifndef RESERVOIR_H
#define RESERVOIR_H

struct Reservoir {
	float total_weight;
	uint M;
	float sample_src_pdf;
	float sample_target_pdf;

	inline float W() {
		if (sample_target_pdf > 0 && M > 0)
			return total_weight / (M * sample_target_pdf);
		else
			return 0;
	}

	SLANG_MUTATING
	inline void init() {
		total_weight = 0;
		M = 0;
		sample_src_pdf = 0;
		sample_target_pdf = 0;
	}

	SLANG_MUTATING
	inline bool update(const float rnd, const float w) {
		M++;
		total_weight += w;
		return rnd*total_weight <= w;
	}

	SLANG_MUTATING
	inline bool update(const float rnd, const float target_pdf, const float src_pdf) {
		M++;
		const float w = target_pdf/src_pdf;
		total_weight += w;
		if (rnd*total_weight <= w) {
			sample_target_pdf = target_pdf;
			sample_src_pdf = src_pdf;
			return true;
		} else
			return false;
	}
};

#endif