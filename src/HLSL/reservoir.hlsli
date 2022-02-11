#ifndef RESERVOIR_H
#define RESERVOIR_H

#include "light.hlsli"

class Reservoir {
	uint4 y;
	float p_hat_y;
	float w_sum;
	uint M;
	float W;

	inline void store(out uint4 out0, out float4 out1) {
		out0 = y;
		out1 = float4(w_sum, asfloat(M), W, p_hat_y);
	}
	inline LightSample load(const uint4 in1, const float4 in2, const DisneyBSDF bsdf, const float3 omega_out, const float3 P, const float3 N, const differential3 dP, const differential3 dD, out float pdf_bsdf, out float pdf_pick, out float3 eval) {
		y = in1;
		w_sum = in2.x;
		M = asuint(in2.y);
		W = in2.z;

		LightSample ls;
		ls.pdf = 0;
		p_hat_y = 0;
		if (w_sum > 0) {
			rng_t tmp_rng = { y };
			ls = sample_light_or_environment(tmp_rng, P, dP, dD, pdf_pick);
			if (ls.pdf > 0) {
				eval = ls.radiance * bsdf.Evaluate(omega_out, N, ls.to_light, pdf_bsdf) * abs(dot(ls.to_light, N)) / ls.pdf;
				p_hat_y = luminance(eval);
			}
		}
		return ls;
	}
	inline LightSample load_prev(const uint4 in1, const float4 in2, const DisneyBSDF bsdf, const float3 omega_out, const float3 P, const float3 N, const differential3 dP, const differential3 dD, out float pdf_bsdf, out float pdf_pick) {
		y = in1;
		w_sum = in2.x;
		M = asuint(in2.y);
		W = in2.z;

		LightSample ls;
		ls.pdf = 0;
		p_hat_y = 0;
		if (w_sum > 0) {
			rng_t tmp_rng = { y };
			ls = sample_light_or_environment(tmp_rng, P, dP, dD, pdf_pick);
			if (ls.pdf > 0)
				p_hat_y = luminance(ls.radiance * bsdf.Evaluate(omega_out, N, ls.to_light, pdf_bsdf)) * abs(dot(ls.to_light, N)) / ls.pdf;
		}
		return ls;
	}

	inline bool update(float u, uint4 x, float w) {
		w_sum += w;
		M++;
		if (u < w/w_sum) {
			y = x;
			return true;
		}
		return false;
	}

	inline void ris(inout LightSample ls, inout rng_t rng, const DisneyBSDF bsdf, const float3 omega_out, const float3 P, const float3 N, const differential3 dP, const differential3 dD) {
		static const uint reservoirSamples = RESERVOIR_SAMPLE_COUNT(gSamplingFlags);
		for (uint i = 0; i < reservoirSamples; i++) {
			uint4 rng_i = rng.v;

			float pdf_pick_i;
			const LightSample ls_i = sample_light_or_environment(rng, P, dP, dD, pdf_pick_i);
			if (ls_i.pdf <= 0) continue;

			float pdf_bsdf_i;
			const float p_hat_i = luminance(ls_i.radiance * bsdf.Evaluate(omega_out, N, ls_i.to_light, pdf_bsdf_i)) * abs(dot(ls_i.to_light, N)) / ls_i.pdf;
			if (update(rng.next(), rng_i, p_hat_i / pdf_pick_i)) {
				ls = ls_i;
				p_hat_y = p_hat_i;
			}
		}
		if (p_hat_y > 0)
			W = w_sum / p_hat_y / M;
		else
			W = 0;
	}
		
	inline void temporal_reuse(inout LightSample ls, inout rng_t rng, const uint2 index, const DisneyBSDF bsdf, const float3 omega_out, const float3 P, const float3 N, const differential3 dP, const differential3 dD) {
		const uint max_M = 20*M;
		uint M_sum = M;

		w_sum = p_hat_y*W*M;
		M = 1;

		float pdf_pick, pdf_bsdf;
		Reservoir q;
		const LightSample ls_prev = q.load_prev(gPrevReservoirRNG[index], gPrevReservoirs[index], bsdf, omega_out, P, N, dP, dD, pdf_bsdf, pdf_pick);
		if (q.M > 0) {
			q.M = min(q.M, max_M);
			if (update(rng.next(), q.y, q.p_hat_y*q.W*q.M)) {
				ls = ls_prev;
				p_hat_y = q.p_hat_y;
			}
			if (q.p_hat_y > 0)
				M_sum += q.M;
		}

		M = M_sum;
		W = w_sum / p_hat_y / M;
	}
	inline void spatial_reuse(inout LightSample ls, const uint2 index, const ViewData view, const DisneyBSDF bsdf, const float3 omega_out, const float3 P, const float3 N, const differential3 dP, const differential3 dD, out float pdf_bsdf, out float pdf_pick) {
		w_sum = p_hat_y*W*M;
		uint M_sum = M;
		uint Z = p_hat_y > 0 ? M : 0;

		const VisibilityInfo v = load_visibility(index.xy);

		rng_t rng = { uint4(index, gPushConstants.gRandomSeed, index.x + index.y) };

		static const uint reservoirSpatialSamples = RESERVOIR_SPATIAL_SAMPLE_COUNT(gSamplingFlags);
		for (uint i = 0; i < reservoirSpatialSamples; i++) {
			const int2 o = 30*(2*float2(rng.next(), rng.next())-1);
			if (all(o == 0)) continue;
			const int2 p = index.xy + o;
			if (!test_inside_screen(p, view)) continue;
		const VisibilityInfo v_p = load_visibility(p);
			if (!test_reprojected_depth(v.z, v_p.z, o, v.dz)) continue;
			if (!test_reprojected_normal(v.normal, v_p.normal())) continue;

			float pdf_pick_i, pdf_bsdf_i;
			Reservoir q;
			float3 eval;
			const LightSample ls_i = q.load(gReservoirRNG[p], gReservoirs[p], bsdf, omega_out, P, N, dP, dD, pdf_bsdf_i, pdf_pick_i, eval);
			q.M = min(q.M, M*20);

			if (update(rng.next(), q.y, q.p_hat_y*q.W*q.M)) {
				ls = ls_i;
				pdf_bsdf = pdf_bsdf_i;
				pdf_pick = pdf_pick_i;
				p_hat_y = q.p_hat_y;
			}
			M_sum += q.M;
			if (q.p_hat_y > 0)
				Z += q.M;
		}

		M = M_sum;
		W = w_sum / p_hat_y / Z;
	}
};

#endif