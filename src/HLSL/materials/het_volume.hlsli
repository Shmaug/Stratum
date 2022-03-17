#ifndef HET_VOLUME_H
#define HET_VOLUME_H

#include "../scene.hlsli"

struct HeterogeneousVolume {
	float3 sigma_s;
	float anisotropy;
	float3 sigma_a;
	float max_density;
	float attenuation_unit;
	
#ifdef __HLSL_VERSION
	uint volume_index;
	
	inline float get_density(const pnanovdb_buf_t buf, inout pnanovdb_readaccessor_t accessor, const pnanovdb_coord_t coord) {
		return pnanovdb_read_float(buf, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, buf, accessor, floor(coord)));
	}

	struct DeltaTrackResult {
		float3 transmittance;
		float3 pdf;
		float3 scatter_p;
	};
	inline DeltaTrackResult delta_track(float3 origin, float3 direction, float t_max, inout rng_t rng, const bool can_scatter = true) {
		DeltaTrackResult r;
		r.transmittance = 1;
		r.pdf = 1;
		r.scatter_p = 1.#INF;

		const float3 majorant = max_density * (sigma_a + sigma_s);
		const uint channel = rng.nexti()%3;
		if (majorant[channel] == 0) return r;

    pnanovdb_buf_t buf = gVolumes[volume_index];
		pnanovdb_grid_handle_t grid = pnanovdb_grid_handle_t(0);
		pnanovdb_readaccessor_t accessor;
		pnanovdb_readaccessor_init(accessor, pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, grid)));

		origin    = pnanovdb_grid_world_to_indexf(buf, grid, origin);
		direction = pnanovdb_grid_world_to_index_dirf(buf, grid, direction);

		for (uint iteration = 0; iteration < gPushConstants.gMaxNullCollisions && any(r.transmittance > 0); iteration++) {
			const float t = attenuation_unit * -log(1 - rng.next()) / majorant[channel];
			if (t < t_max) {
				origin += direction*t;
				t_max -= t;
				const float3 local_density = get_density(buf, accessor, origin);
				const float3 sigma_t = local_density * (sigma_a + sigma_s);
				const float3 real_prob = sigma_t / majorant;
				const float3 tr = exp(-majorant * t) / max3(majorant);
				if (can_scatter && rng.next() < real_prob[channel]) {
					// real particle
					r.transmittance *= tr * sigma_s * local_density;
					r.pdf *= tr * majorant * real_prob;
					r.scatter_p = pnanovdb_grid_index_to_worldf(buf, grid, origin);
					break;
				} else {
					// fake particle
					r.transmittance *= tr * (majorant - sigma_t);
					r.pdf *= tr * majorant;
					if (can_scatter) r.pdf *= 1 - real_prob;
				}
			} else {
				// transmitted without scattering
				const float3 tr = exp(-majorant * t_max);
				r.transmittance *= tr;
				r.pdf *= tr;
				break;
			}
		}
		return r;
	}

	template<bool TransportToLight>
	inline BSDFEvalRecord eval(const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
		const Real v = 1/(4*M_PI) * (1 - anisotropy * anisotropy) / pow(1 + anisotropy * anisotropy + 2 * anisotropy * dot(dir_in, dir_out), 1.5);
		BSDFEvalRecord r;
		r.f = v;
		r.pdfW = v;
		return r;
	}

	template<bool TransportToLight>
	inline BSDFSampleRecord sample(const Vector3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex) {
		BSDFSampleRecord r;
		if (abs(anisotropy) < 1e-3) {
			const Real z = 1 - 2 * rnd.x;
			const Real phi = 2 * M_PI * rnd.y;
			r.dir_out = Vector3(sqrt(max(0, 1 - z * z)) * float2(cos(phi), sin(phi)), z);
    } else {
			const Real tmp = (anisotropy * anisotropy - 1) / (2 * rnd.x * anisotropy - (anisotropy + 1));
			const Real cos_elevation = (tmp * tmp - (1 + anisotropy * anisotropy)) / (2 * anisotropy);
			const Real sin_elevation = sqrt(max(1 - cos_elevation * cos_elevation, 0));
			const Real azimuth = 2 * M_PI * rnd.y;
			ShadingFrame frame;
			frame.n = dir_in;
			make_orthonormal(frame.n, frame.t, frame.b);
			r.dir_out = frame.to_world(float3(sin_elevation * cos(azimuth), sin_elevation * sin(azimuth), cos_elevation));
    }
		r.eta = -1;
		r.eval = eval<TransportToLight>(dir_in, r.dir_out, vertex);
		return r;
	}

	inline Spectrum eval_albedo  (const PathVertexGeometry vertex) { return sigma_s; }
	inline Spectrum eval_emission(const PathVertexGeometry vertex) { return 0; }

	inline void load(ByteAddressBuffer bytes, inout uint address) {
		sigma_s          = bytes.Load<float3>(address); address += 12;
		anisotropy       = bytes.Load<float>(address); address += 4;
		sigma_a          = bytes.Load<float3>(address); address += 12;
		max_density      = bytes.Load<float>(address); address += 4;
		attenuation_unit = bytes.Load<float>(address); address += 4;
		volume_index     = bytes.Load(address); address += 4;
	}

#endif // __HLSL_VERSION

#ifdef __cplusplus
	component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>> handle;
	Buffer::View<byte> buffer;

	inline void store(ByteAppendBuffer& bytes, ResourcePool& pool) const {
		bytes.AppendN(sigma_s);
		bytes.Appendf(anisotropy);
		bytes.AppendN(sigma_a);
		bytes.Appendf(max_density);
		bytes.Appendf(attenuation_unit);
		bytes.Append(pool.get_index(buffer));
	}
	inline void inspector_gui() {
		ImGui::ColorEdit3("Scattering", sigma_s.data(), ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Absorption", sigma_a.data(), ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Anisotropy", &anisotropy, -.999f, .999f);
		ImGui::SliderFloat("Attenuation Factor", &attenuation_unit, 0, 1.f);
	}

#endif
};

#endif