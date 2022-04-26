#ifndef RESERVOIR_H
#define RESERVOIR_H

#include "scene.hlsli"

struct ReservoirLightSample {
	float3 local_position;
	uint instance_primitive_index;
	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
};

struct Reservoir {
	ReservoirLightSample light_sample;
	uint M;
	float total_weight;
	float sample_target_pdf; // p_q_hat
	float sample_source_pdf;

	inline float W() CONST_CPP { return (M == 0 || sample_target_pdf == 0) ? 0 : (total_weight / M) / sample_target_pdf; }

	inline bool update(const float rnd, const ReservoirLightSample candidate_sample, const float source_pdf, const float target_pdf) {
		const float ris_weight = target_pdf / source_pdf;
		M++;
		total_weight += ris_weight;
		if (rnd < ris_weight/total_weight) {
			light_sample = candidate_sample;
			sample_target_pdf = target_pdf;
			return true;
		}
		return false;
	}
	inline bool update(const float rnd, const Reservoir r) {
		const float w = r.W();
		total_weight += w;
		M++;
		if (rnd < w/total_weight) {
			light_sample = r.light_sample;
			sample_target_pdf = r.sample_target_pdf;
			return true;
		}
		return false;
	}
};

#ifdef __HLSL__
inline void init_reservoir(out Reservoir r) {
	BF_SET(r.light_sample.instance_primitive_index, INVALID_INSTANCE, 0, 16);
	BF_SET(r.light_sample.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
	r.M = 0;
	r.sample_target_pdf = 0;
	r.total_weight = 0;
}

inline Reservoir merge(const float rnd1, const float rnd2, const Reservoir r1, const Reservoir r2) {
	Reservoir merged;
	init_reservoir(merged);
	merged.update(rnd1, r1);
	merged.update(rnd2, r2);
	merged.M = r1.M + r2.M;
	return merged;
}
#endif

#endif