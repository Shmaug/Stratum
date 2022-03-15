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
	
	inline pnanovdb_readaccessor_t accessor() {
		pnanovdb_buf_t buf = gVolumes[volume_index];
		pnanovdb_grid_handle_t grid = pnanovdb_grid_handle_t(0);
		pnanovdb_root_handle_t root = pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, grid));
		pnanovdb_readaccessor_t accessor;
		pnanovdb_readaccessor_init(accessor, root);
		return accessor;
	}

	struct DeltaTrackResult {
		float3 transmittance;
		float3 pdf;
		float3 scatter_p;
		float3 scatter_s;
	};
	inline DeltaTrackResult delta_track(inout pnanovdb_readaccessor_t accessor, float3 origin, float3 direction, const float t_max, inout rng_t rng, const bool can_scatter = true) {
		DeltaTrackResult r;
		r.transmittance = 1;
		r.pdf = 1;
		r.scatter_s = 0;

    pnanovdb_buf_t buf = gVolumes[volume_index];
    pnanovdb_grid_handle_t grid = pnanovdb_grid_handle_t(0);

		origin    = pnanovdb_grid_world_to_indexf(buf, grid, origin);
		direction = pnanovdb_grid_world_to_index_dirf(buf, grid, direction);

		const float3 majorant = max_density * (sigma_a + sigma_s);
		const uint channel = rng.nexti()%3;
		if (majorant[channel] > 0) {
			float accum_t = 0;
			for (uint iteration = 0; iteration < gPushConstants.gMaxNullCollisions && any(r.transmittance > 0); iteration++) {
				const float t = attenuation_unit * -log(1 - rng.next()) / majorant[channel];
				const float dt = t_max - accum_t;
				accum_t = min(accum_t + t, t_max);
				if (t < dt) {
					const float3 cur_p = origin + direction * accum_t;
					const float3 local_density = pnanovdb_read_float(buf, pnanovdb_readaccessor_get_value_address(PNANOVDB_GRID_TYPE_FLOAT, buf, accessor, cur_p));
					const float3 sigma_t = local_density * (sigma_a + sigma_s);
					const float3 real_prob = sigma_t / majorant;
					if (can_scatter && rng.next() < real_prob[channel]) {
						// real particle
						const float3 tr = exp(-majorant * t) / max3(majorant);
						r.transmittance *= tr;
						r.pdf *= tr * majorant * real_prob;
						r.scatter_p = pnanovdb_grid_index_to_worldf(buf, grid, cur_p);
						r.scatter_s = sigma_s * local_density;
						break;
					} else {
						// fake particle
						const float3 tr = exp(-majorant * t) / max3(majorant);
						r.transmittance *= tr * (majorant - sigma_t);
						r.pdf *= tr * majorant;
						if (can_scatter) r.pdf *= 1 - real_prob;
					}
				} else {
					// transmitted without scattering
					if (dt > 0) {
						const float3 tr = exp(-majorant * dt);
						r.transmittance *= tr;
						r.pdf *= tr;
					}
					break;
				}
			}
		}
		return r;
	}

	template<bool TransportToLight, typename Real, typename Real3>
	inline BSDFEvalRecord<Real3> eval(const Real3 dir_in, const Real3 dir_out, const PathVertexGeometry vertex) {
		const Real v = 1/(4*M_PI) * (1 - anisotropy * anisotropy) / pow(1 + anisotropy * anisotropy + 2 * anisotropy * dot(dir_in, dir_out), 1.5);
		BSDFEvalRecord<Real3> r;
		r.f = v;
		r.pdfW = v;
		return r;
	}

	template<bool TransportToLight, typename Real, typename Real3>
	inline BSDFSampleRecord<Real3> sample(const Real3 rnd, const Real3 dir_in, const PathVertexGeometry vertex) {
		BSDFSampleRecord<Real3> r;
		if (abs(anisotropy) < 1e-3) {
			const Real z = 1 - 2 * rnd.x;
			const Real phi = 2 * M_PI * rnd.y;
			r.dir_out = Real3(sqrt(max(0, 1 - z * z)) * float2(cos(phi), sin(phi)), z);
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
		r.eval = eval<TransportToLight, Real, Real3>(dir_in, r.dir_out, vertex);
		return r;
	}

	template<typename Real3> inline Real3 eval_albedo  (const PathVertexGeometry vertex) { return 0; }
	template<typename Real3> inline Real3 eval_emission(const PathVertexGeometry vertex) { return 0; }

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
		ImGui::SliderFloat("Anisotropy", &anisotropy, -1.f, 1.f);
		ImGui::SliderFloat("Attenuation Factor", &attenuation_unit, 0, 1.f);
	}

#endif
};

#endif