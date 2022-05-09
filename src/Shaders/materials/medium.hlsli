#include "material.h"

struct Medium {
	Spectrum density_scale;
	float anisotropy;
	Spectrum albedo_scale;
	float attenuation_unit;
	uint density_volume_index;
	uint albedo_volume_index;

	SLANG_MUTATING
	inline void load(uint address) {
		density_scale		 = gMaterialData.Load<float3>(address); address += 12;
		anisotropy       	 = gMaterialData.Load<float>(address); address += 4;
		albedo_scale         = gMaterialData.Load<float3>(address); address += 12;
		attenuation_unit 	 = gMaterialData.Load<float>(address); address += 4;
		density_volume_index = gMaterialData.Load(address); address += 4;
		albedo_volume_index  = gMaterialData.Load(address); address += 4;
	}

	inline Spectrum read_density(inout pnanovdb_readaccessor_t density_accessor, pnanovdb_address_t address) {
		return pnanovdb_read_float(gVolumes[density_volume_index], address);
	}
	inline Spectrum read_density(inout pnanovdb_readaccessor_t density_accessor, const Vector3 pos_index) {
		return read_density(density_accessor, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[density_volume_index], density_accessor, floor(pos_index)));
	}
	inline Spectrum read_albedo(inout pnanovdb_readaccessor_t density_accessor, pnanovdb_address_t address) {
		return pnanovdb_read_float(gVolumes[albedo_volume_index], address);
	}
	inline Spectrum read_albedo(inout pnanovdb_readaccessor_t albedo_accessor, const Vector3 pos_index) {
		if (albedo_volume_index == -1)
			return 1;
		else
			return read_albedo(albedo_accessor, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[albedo_volume_index], albedo_accessor, floor(pos_index)));
	}

	// returns hit position inside medium. multiplies beta by transmittance*sigma_s
	inline Vector3 delta_track(inout rng_state_t rng_state, Vector3 origin, Vector3 direction, float t_max, inout Spectrum beta, inout Spectrum dir_pdf, inout Spectrum nee_pdf, const bool can_scatter = true) {
		// TODO: slang crashes when compiling this??
#ifndef __SLANG__
		pnanovdb_readaccessor_t density_accessor, albedo_accessor;
		pnanovdb_grid_handle_t grid_handle = { 0 };
		pnanovdb_readaccessor_init(density_accessor, pnanovdb_tree_get_root(gVolumes[density_volume_index], pnanovdb_grid_get_tree(gVolumes[density_volume_index], grid_handle)));
		if (albedo_volume_index != -1)
			pnanovdb_readaccessor_init(albedo_accessor, pnanovdb_tree_get_root(gVolumes[albedo_volume_index], pnanovdb_grid_get_tree(gVolumes[albedo_volume_index], grid_handle)));

		const Spectrum majorant = density_scale * read_density(density_accessor, pnanovdb_root_get_max_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[density_volume_index], density_accessor.root));
		const uint channel = rng_next_uint(rng_state)%3;
		if (majorant[channel] == 0) return 0/0;

		origin    = pnanovdb_grid_world_to_indexf(gVolumes[density_volume_index], grid_handle, origin);
		direction = pnanovdb_grid_world_to_index_dirf(gVolumes[density_volume_index], grid_handle, direction);

		for (uint iteration = 0; iteration < gMaxNullCollisions && any(beta > 0); iteration++) {
			const float2 rnd = float2(rng_next_float(rng_state), rng_next_float(rng_state));
			const float t = attenuation_unit * -log(1 - rnd.x) / majorant[channel];
			if (t < t_max) {
				origin += direction*t;
				t_max -= t;
				const Spectrum local_density = density_scale * read_density(density_accessor, origin);
				const Spectrum local_albedo  = albedo_scale * read_albedo(albedo_accessor, origin);
				const Spectrum local_sigma_s = local_density * local_albedo;
				const Spectrum local_sigma_a = local_density * (1 - local_albedo);
				const Spectrum local_sigma_t = local_sigma_s + local_sigma_a;
				const Spectrum real_prob = local_sigma_t / majorant;
				const Spectrum tr = exp(-majorant * t) / max3(majorant);
				if (can_scatter && rnd.y < real_prob[channel]) {
					// real particle
					beta *= tr * local_sigma_s;
					dir_pdf *= tr * majorant * real_prob;
					return pnanovdb_grid_index_to_worldf(gVolumes[density_volume_index], grid_handle, origin);
				} else {
					// fake particle
					beta *= tr * (majorant - local_sigma_t);
					dir_pdf *= tr * majorant * (1 - real_prob);
					nee_pdf *= tr * majorant;
					return 0/0;
				}
			} else {
				// transmitted without scattering
				const Spectrum tr = exp(-majorant * t_max);
				beta *= tr;
				nee_pdf *= tr;
				dir_pdf *= tr;
				break;
			}
		}
#endif
		return POS_INFINITY;
	}

	inline void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out) {
		const Real v = 1/(4*M_PI) * (1 - anisotropy * anisotropy) / pow(1 + anisotropy * anisotropy + 2 * anisotropy * dot(dir_in, dir_out), 1.5);
		r.f = v;
		r.pdf_fwd = v;
		r.pdf_rev = v;
	}

	inline void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in) {
		if (abs(anisotropy) < 1e-3) {
			const Real z = 1 - 2 * rnd.x;
			const Real phi = 2 * M_PI * rnd.y;
			r.dir_out = Vector3(sqrt(max(0, 1 - z * z)) * float2(cos(phi), sin(phi)), z);
		} else {
			const Real tmp = (anisotropy * anisotropy - 1) / (2 * rnd.x * anisotropy - (anisotropy + 1));
			const Real cos_elevation = (tmp * tmp - (1 + anisotropy * anisotropy)) / (2 * anisotropy);
			const Real sin_elevation = sqrt(max(1 - cos_elevation * cos_elevation, 0));
			const Real azimuth = 2 * M_PI * rnd.y;
			float3 t, b;
			make_orthonormal(dir_in, t, b);
			r.dir_out = sin_elevation * cos(azimuth) * t + sin_elevation * sin(azimuth) * b + cos_elevation * dir_in;
		}
		const Real v = 1/(4*M_PI) * (1 - anisotropy * anisotropy) / pow(1 + anisotropy * anisotropy + 2 * anisotropy * dot(dir_in, r.dir_out), 1.5);
		r.pdf_fwd = v;
		r.pdf_rev = v;
		r.eta = -1;
		r.roughness = 1 - abs(anisotropy);
	}
};