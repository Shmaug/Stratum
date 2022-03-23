#ifndef HET_VOLUME_H
#define HET_VOLUME_H

#include "../scene.hlsli"

struct HeterogeneousVolume {
	float3 density_scale;
	float anisotropy;
	float3 albedo_scale;
	float attenuation_unit;
	
#ifdef __cplusplus
	component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>> density_grid, albedo_grid;
	Buffer::View<byte> density_buffer, albedo_buffer;

	inline void store(ByteAppendBuffer& bytes, ResourcePool& pool) const {
		bytes.AppendN(density_scale);
		bytes.Appendf(anisotropy);
		bytes.AppendN(albedo_scale);
		bytes.Appendf(attenuation_unit);
		bytes.Append(pool.get_index(density_buffer));
		bytes.Append(pool.get_index(albedo_buffer));
	}
	inline void inspector_gui() {
		ImGui::ColorEdit3("Density", density_scale.data(), ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Albedo", albedo_scale.data(), ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Anisotropy", &anisotropy, -.999f, .999f);
		ImGui::SliderFloat("Attenuation Unit", &attenuation_unit, 0, 1);
	}
#endif
#ifdef __HLSL_VERSION
	uint density_volume_index;
	uint albedo_volume_index;

	inline void load(ByteAddressBuffer bytes, inout uint address) {
		density_scale 			 = bytes.Load<float3>(address); address += 12;
		anisotropy       		 = bytes.Load<float>(address); address += 4;
		albedo_scale         = bytes.Load<float3>(address); address += 12;
		attenuation_unit 		 = bytes.Load<float>(address); address += 4;
		density_volume_index = bytes.Load(address); address += 4;
		albedo_volume_index  = bytes.Load(address); address += 4;
	}

	inline float3 read_density(inout pnanovdb_readaccessor_t density_accessor, pnanovdb_address_t address) {
		return pnanovdb_read_float(gVolumes[density_volume_index], address);
	}
	inline float3 read_density(inout pnanovdb_readaccessor_t density_accessor, const float3 pos_index) {
		return read_density(density_accessor, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[density_volume_index], density_accessor, floor(pos_index)));
	}
	inline float3 read_albedo(inout pnanovdb_readaccessor_t density_accessor, pnanovdb_address_t address) {
		return pnanovdb_read_float(gVolumes[albedo_volume_index], address);
	}
	inline float3 read_albedo(inout pnanovdb_readaccessor_t albedo_accessor, const float3 pos_index) {
		if (albedo_volume_index == -1)
			return 1;
		else
			return read_albedo(albedo_accessor, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[albedo_volume_index], albedo_accessor, floor(pos_index)));
	}
	
	struct DeltaTrackResult {
		float3 transmittance; // includes sigma_s, if scattering occured
		float3 dir_pdf;
		float3 nee_pdf;
		float3 scatter_p;
	};
	inline DeltaTrackResult delta_track(float3 origin, float3 direction, float t_max, const uint index_1d, const bool can_scatter = true) {
		DeltaTrackResult r;
		r.transmittance = 1;
		r.dir_pdf = 1;
		r.nee_pdf = 1;
		r.scatter_p = 1.#INF;

		pnanovdb_readaccessor_t density_accessor, albedo_accessor;
		pnanovdb_readaccessor_init(density_accessor, pnanovdb_tree_get_root(gVolumes[density_volume_index], pnanovdb_grid_get_tree(gVolumes[density_volume_index], pnanovdb_grid_handle_t(0))));
		if (albedo_volume_index != -1)
			pnanovdb_readaccessor_init(albedo_accessor, pnanovdb_tree_get_root(gVolumes[albedo_volume_index], pnanovdb_grid_get_tree(gVolumes[albedo_volume_index], pnanovdb_grid_handle_t(0))));
	
		const float3 majorant = density_scale * read_density(density_accessor, pnanovdb_root_get_max_address(PNANOVDB_GRID_TYPE_FLOAT, gVolumes[density_volume_index], density_accessor.root));
		const uint channel = rng_next_uint(index_1d)%3;
		if (majorant[channel] == 0) return r;

		origin    = pnanovdb_grid_world_to_indexf(gVolumes[density_volume_index], pnanovdb_grid_handle_t(0), origin);
		direction = pnanovdb_grid_world_to_index_dirf(gVolumes[density_volume_index], pnanovdb_grid_handle_t(0), direction);

		for (uint iteration = 0; iteration < gPushConstants.gMaxNullCollisions && any(r.transmittance > 0); iteration++) {
			const float2 rnd = float2(rng_next_float(index_1d), rng_next_float(index_1d));
			const float t = attenuation_unit * -log(1 - rnd.x) / majorant[channel];
			if (t < t_max) {
				origin += direction*t;
				t_max -= t;
				const float3 local_density = density_scale * read_density(density_accessor, origin);
				const float3 local_albedo  = albedo_scale * read_albedo(albedo_accessor, origin);
				const float3 local_sigma_s = local_density * local_albedo;
				const float3 local_sigma_a = local_density * (1 - local_albedo);
				const float3 local_sigma_t = local_sigma_s + local_sigma_a;
				const float3 real_prob = local_sigma_t / majorant;
				const float3 tr = exp(-majorant * t) / max3(majorant);
				if (can_scatter && rnd.y < real_prob[channel]) {
					// real particle
					r.transmittance *= tr * local_sigma_s;
					r.dir_pdf *= tr * majorant * real_prob;
					r.scatter_p = pnanovdb_grid_index_to_worldf(gVolumes[density_volume_index], pnanovdb_grid_handle_t(0), origin);
					break;
				} else {
					// fake particle
					r.transmittance *= tr * (majorant - local_sigma_t);
					r.dir_pdf *= tr * majorant * (1 - real_prob);
					r.nee_pdf *= tr * majorant;
				}
			} else {
				// transmitted without scattering
				const float3 tr = exp(-majorant * t_max);
				r.transmittance *= tr;
				r.nee_pdf *= tr;
				r.dir_pdf *= tr;
				break;
			}
		}
		return r;
	}
#endif // __HLSL_VERSION
};

#ifdef __HLSL_VERSION
template<> inline BSDFEvalRecord eval_material(const HeterogeneousVolume material, const Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
	const Real v = 1/(4*M_PI) * (1 - material.anisotropy * material.anisotropy) / pow(1 + material.anisotropy * material.anisotropy + 2 * material.anisotropy * dot(dir_in, dir_out), 1.5);
	BSDFEvalRecord r;
	r.f = v;
	r.pdfW = v;
	return r;
}

template<> inline BSDFSampleRecord sample_material(const HeterogeneousVolume material, const Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
	BSDFSampleRecord r;
	if (abs(material.anisotropy) < 1e-3) {
		const Real z = 1 - 2 * rnd.x;
		const Real phi = 2 * M_PI * rnd.y;
		r.dir_out = Vector3(sqrt(max(0, 1 - z * z)) * float2(cos(phi), sin(phi)), z);
	} else {
		const Real tmp = (material.anisotropy * material.anisotropy - 1) / (2 * rnd.x * material.anisotropy - (material.anisotropy + 1));
		const Real cos_elevation = (tmp * tmp - (1 + material.anisotropy * material.anisotropy)) / (2 * material.anisotropy);
		const Real sin_elevation = sqrt(max(1 - cos_elevation * cos_elevation, 0));
		const Real azimuth = 2 * M_PI * rnd.y;
		ShadingFrame frame;
		frame.n = dir_in;
		make_orthonormal(frame.n, frame.t, frame.b);
		r.dir_out = frame.to_world(float3(sin_elevation * cos(azimuth), sin_elevation * sin(azimuth), cos_elevation));
	}
	r.eta = -1;
	r.eval = eval_material(material, dir_in, r.dir_out, vertex, dir);
	return r;
}

template<> inline Spectrum eval_material_albedo(const HeterogeneousVolume material, const uint vertex) { return material.albedo_scale; }
#endif

#endif