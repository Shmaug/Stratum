#ifndef RESERVOIR_H
#define RESERVOIR_H

struct Reservoir {
	float total_weight;
	uint M;

	inline float W(const float sample_target_pdf) {
		if (sample_target_pdf > 0 && M > 0)
			return total_weight / (M * sample_target_pdf);
		else
			return 0;
	}

	SLANG_MUTATING
	inline void init() {
		total_weight = 0;
		M = 0;
	}

	SLANG_MUTATING
	inline bool update(const float rnd, const float w) {
		M++;
		total_weight += w;
		return rnd*total_weight <= w;
	}
};

#endif