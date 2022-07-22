#ifndef RESERVOIR_H
#define RESERVOIR_H

struct Reservoir {
	float total_weight;
	uint M;
	float W;
	float src_pdf;

	SLANG_MUTATING
	inline void init() {
		total_weight = 0;
		M = 0;
		W = 0;
		src_pdf = 0;
	}

	SLANG_MUTATING
	inline bool update(const float rnd, const float w) {
		M++;
		total_weight += w;
		return rnd*total_weight <= w;
	}
};

#endif