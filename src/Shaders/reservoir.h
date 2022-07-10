#ifndef RESERVOIR_H
#define RESERVOIR_H

struct Reservoir {
	uint M;
	float total_weight;
	float W;

	SLANG_MUTATING
	inline void init() {
		M = 0;
		total_weight = 0;
		W = 0;
	}

	SLANG_MUTATING
	inline bool update(const float rnd, const float w) {
		M++;
		total_weight += w;
		return rnd*total_weight <= w;
	}
};

#endif