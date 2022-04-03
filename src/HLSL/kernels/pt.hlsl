#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_light_paths
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E integrate_indirect

#define gVolumeCount 8
#define gImageCount 1024

#include "pt_descriptors.hlsli"

[[vk::constant_id(0)]] const uint gDebugMode = 0;
[[vk::constant_id(1)]] const uint gSamplingFlags = 0xC3;

#define gSampleEnvironment		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_ENVIRONMENT)
#define gSampleEmissive 		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_EMISSIVE)
#define gTraceLightPaths		(gSamplingFlags & SAMPLE_FLAG_TRACE_LIGHT_PATHS)
#define gSampleLightPaths		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
#define gUniformSphereSampling 	(gSamplingFlags & SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING)
#define gSampleReservoirs		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_RESERVOIRS)
#define gUseMIS 				(gSamplingFlags & SAMPLE_FLAG_MIS)
#define gUseRayCones 			(gSamplingFlags & SAMPLE_FLAG_RAY_CONE_LOD)
#define gEnableVolumes 			(gSamplingFlags & SAMPLE_FLAG_ENABLE_VOLUMES)

[[vk::push_constant]] const PathTracePushConstants gPushConstants;

#include "../tonemap.hlsli"
#include "../image_value.hlsli"

#define PNANOVDB_HLSL
#include "../../extern/nanovdb/PNanoVDB.h"

#define gPathState gPathStates[index_1d]
#define gPathVertex gPathVertices[index_1d]

inline uint4 pcg4d(uint4 v) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v = v ^ (v >> 16u);
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}
inline uint rng_next_uint(const uint index_1d) {
	gPathState.rng_state.w++;
	return pcg4d(gPathState.rng_state).x;
}
inline float rng_next_float(const uint index_1d) { return asfloat(0x3f800000 | (rng_next_uint(index_1d) >> 9)) - 1; }

#include "../material.hlsli"
#include "../light.hlsli"

inline bool is_volume(const uint instance_index) {
	const uint material_address = instance_index == INVALID_INSTANCE ? gPushConstants.gEnvironmentMaterialAddress : gInstances[instance_index].material_address();
	if (material_address == -1) return false;
	const uint type = gMaterialData.Load(material_address);
	return type == BSDFType::eHeterogeneousVolume;
}
inline uint path_material_address(const uint index_1d) {
	const uint instance_index = gPathState.instance_index();
	if (instance_index == INVALID_INSTANCE)
		return gPushConstants.gEnvironmentMaterialAddress;
	else
		return gInstances[instance_index].material_address();
}
inline RayDifferential path_ray(const uint index_1d) {
	RayDifferential r;
	r.origin = gPathState.ray_origin;
	r.direction = normalize(gPathVertex.position - r.origin);
	r.t_min = 0;
	r.t_max = 1.#INF;
	r.radius = gPathState.radius();
	r.spread = gPathState.spread();
	return r;
}

#define ray_query_t RayQuery<RAY_FLAG_NONE>
inline bool do_ray_query(inout ray_query_t rayQuery, const RayDifferential ray, out float3 vol_normal) {
	gCounters.InterlockedAdd(COUNTER_ADDRESS_RAY_COUNT, 1);
	RayDesc rayDesc;
	rayDesc.Origin = ray.origin;
	rayDesc.Direction = ray.direction;
	rayDesc.TMin = ray.t_min;
	rayDesc.TMax = ray.t_max;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, rayDesc);
	while (rayQuery.Proceed()) {
		switch (rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE: {
				const InstanceData instance = gInstances[rayQuery.CandidateInstanceID()];
				switch (instance.type()) {
					case INSTANCE_TYPE_SPHERE: {
						const float2 st = ray_sphere(rayQuery.CandidateObjectRayOrigin(), rayQuery.CandidateObjectRayDirection(), 0, 1);
						if (st.x < st.y) {
							const float t = st.x > rayQuery.RayTMin() ? st.x : st.y;
							if (t < rayQuery.CommittedRayT() && t > rayQuery.RayTMin())
								rayQuery.CommitProceduralPrimitiveHit(t);
						}
						break;
					}
					case INSTANCE_TYPE_VOLUME: {
						pnanovdb_buf_t buf = gVolumes[instance.volume_index()];
						pnanovdb_root_handle_t root = pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, pnanovdb_grid_handle_t(0)));
						const float3 origin    = pnanovdb_grid_world_to_indexf(buf, pnanovdb_grid_handle_t(0), rayQuery.CandidateObjectRayOrigin());
						const float3 direction = pnanovdb_grid_world_to_index_dirf(buf, pnanovdb_grid_handle_t(0), rayQuery.CandidateObjectRayDirection());
						const pnanovdb_coord_t bbox_min = pnanovdb_root_get_bbox_min(buf, root);
						const pnanovdb_coord_t bbox_max = pnanovdb_root_get_bbox_max(buf, root) + 1;
						const float3 t0 = (bbox_min - origin) / direction;
						const float3 t1 = (bbox_max - origin) / direction;
						const float3 tmin = min(t0, t1);
						const float3 tmax = max(t0, t1);
						const float2 st = float2(max3(tmin), min3(tmax));
						const float t = st.x > rayQuery.RayTMin() ? st.x : st.y;
						if (t < rayQuery.CommittedRayT() && t > rayQuery.RayTMin()) {
							rayQuery.CommitProceduralPrimitiveHit(t);
							vol_normal = -(t == t0) + (t == t1);
							vol_normal = normalize(instance.transform.transform_vector(pnanovdb_grid_index_to_world_dirf(buf, pnanovdb_grid_handle_t(0), vol_normal)));
						}
						break;
					}
				}
			}
			case CANDIDATE_NON_OPAQUE_TRIANGLE: {
				//uint instance_primitive_index = 0;
				//BF_SET(instance_primitive_index, rayQuery.CandidateInstanceID(), 0, 16);
				//BF_SET(instance_primitive_index, rayQuery.CandidatePrimitiveIndex(), 16, 16);
				//PathVertex v;
				//make_vertex(instance_primitive_index, ray.origin + ray.direction*rayQuery.CandidateTriangleRayT(), rayQuery.CandidateTriangleBarycentrics(), ray, v);
				// TODO: test alpha
				rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}
	return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
}
inline void intersect(inout ray_query_t rayQuery, const RayDifferential ray, out PathVertexGeometry v, out uint instance_primitive_index) {
	float3 vol_normal;
	if (do_ray_query(rayQuery, ray, vol_normal)) {
		// hit an instance
		BF_SET(instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
			const float3 local_pos = rayQuery.CommittedObjectRayOrigin() + rayQuery.CommittedObjectRayDirection()*rayQuery.CommittedRayT();
			switch (gInstances[rayQuery.CommittedInstanceID()].type()) {
				case INSTANCE_TYPE_SPHERE:
					instance_sphere_geometry(v, rayQuery.CommittedInstanceID(), local_pos);
					break;
				case INSTANCE_TYPE_VOLUME:
					instance_volume_geometry(v, rayQuery.CommittedInstanceID(), local_pos);
					v.geometry_normal = v.shading_normal = vol_normal;
					break;
			}
		} else {
			// triangle
			BF_SET(instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			instance_triangle_geometry(v, rayQuery.CommittedInstanceID(), load_tri(gIndices, gInstances[rayQuery.CommittedInstanceID()], rayQuery.CommittedPrimitiveIndex()), rayQuery.CommittedTriangleBarycentrics());
		}
		v.front_face = dot(ray.direction, v.geometry_normal) < 0;
		v.uv_screen_size = ray.differential_transfer(rayQuery.CommittedRayT()) / v.inv_uv_size;
	} else {
		// background
		BF_SET(instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		v.position = ray.direction;
		v.geometry_normal = v.shading_normal = ray.direction;
		v.tangent.w = 1;
		v.shape_area = 0;
		v.uv = cartesian_to_spherical_uv(ray.direction);
		v.mean_curvature = 1;
		v.inv_uv_size = 1;
		v.uv_screen_size = ray.radius / v.inv_uv_size;
	}
}
inline void intersect_and_scatter(inout ray_query_t rayQuery, const RayDifferential ray, const uint index_1d, out float transmit_dir_pdf, out float transmit_nee_pdf) {
	transmit_dir_pdf = 1;
	transmit_nee_pdf = 1;
	RayDifferential tmp_ray = ray;
	for (uint steps = 0; steps < 64; steps++) {
		intersect(rayQuery, tmp_ray, gPathVertex, gPathState.instance_primitive_index);

		if (!gEnableVolumes) return;
		
		if (is_volume(gPathState.vol_index)) {
			// interact with volume
			const HeterogeneousVolume::DeltaTrackResult tr = load_material<HeterogeneousVolume>(gInstances[gPathState.vol_index].material_address() + 4, index_1d).delta_track(
				gInstances[gPathState.vol_index].inv_transform.transform_point(tmp_ray.origin),
				gInstances[gPathState.vol_index].inv_transform.transform_vector(tmp_ray.direction),
				rayQuery.CommittedRayT(), index_1d);
			gPathState.throughput *= tr.transmittance / average(tr.dir_pdf);
			transmit_dir_pdf *= average(tr.dir_pdf);
			transmit_nee_pdf *= average(tr.nee_pdf);
			if (all(isfinite(tr.scatter_p))) {
				uint instance_primitive_index = 0;
				BF_SET(instance_primitive_index, gPathState.vol_index, 0, 16);
				BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				gPathState.instance_primitive_index = instance_primitive_index;
				instance_volume_geometry(gPathVertex, gPathState.vol_index, tr.scatter_p);
				gPathVertex.uv_screen_size = ray.differential_transfer(length(ray.origin - gPathVertex.position)) / gPathVertex.inv_uv_size;
				return;
			}
		}

		if (gPathState.instance_index() != INVALID_INSTANCE && is_volume(gPathState.instance_index())) {
			gPathState.vol_index = gPathVertex.front_face ? gPathState.instance_index() : INVALID_INSTANCE;
			tmp_ray.origin = ray_offset(gPathVertex.position, gPathVertex.front_face ? -gPathVertex.geometry_normal : gPathVertex.geometry_normal);
		} else
			return;
	}
}
inline void eval_transmittance(inout ray_query_t rayQuery, const uint index_1d, const float3 direction, const float t_max, inout float3 transmittance, inout float dir_pdf, inout float nee_pdf) {
	uint cur_vol_instance = gPathState.vol_index;

	const bool inside = dot(direction, gPathVertex.geometry_normal) < 0;
	RayDifferential ray;
	ray.origin = gPathVertex.shape_area == 0 ? gPathVertex.position : ray_offset(gPathVertex.position, inside ? -gPathVertex.geometry_normal : gPathVertex.geometry_normal);
	ray.direction = direction;
	ray.t_min = 0;
	ray.t_max = t_max*0.999;
	while (ray.t_max > 1e-6f) {
		PathVertexGeometry v;
		uint instance_primitive_index;
		intersect(rayQuery, ray, v, instance_primitive_index);
		
		if (!gEnableVolumes) {
			if (BF_GET(instance_primitive_index,0,16) != INVALID_INSTANCE) {
				// hit a surface
				transmittance = 0;
				dir_pdf = 0;
				nee_pdf = 0;
			}
			break;
		}

		const float dt = (BF_GET(instance_primitive_index, 0, 16) == INVALID_INSTANCE) ? ray.t_max : length(v.position - ray.origin);

		if (is_volume(cur_vol_instance)) {
			// interact with volume
			const HeterogeneousVolume::DeltaTrackResult vt = load_material<HeterogeneousVolume>(gInstances[cur_vol_instance].material_address() + 4, index_1d).delta_track(
				gInstances[cur_vol_instance].inv_transform.transform_point(ray.origin),
				gInstances[cur_vol_instance].inv_transform.transform_vector(ray.direction),
				dt, index_1d, false);
			transmittance *= vt.transmittance;
			dir_pdf *= average(vt.dir_pdf);
			nee_pdf *= average(vt.nee_pdf);
		}

		if (BF_GET(instance_primitive_index,0,16) == INVALID_INSTANCE) break;
		
		if (is_volume(BF_GET(instance_primitive_index,0,16))) {
			// hit a volume
			cur_vol_instance = v.front_face ? BF_GET(instance_primitive_index,0,16) : INVALID_INSTANCE;
			ray.origin = ray_offset(v.position, v.front_face ? -v.geometry_normal : v.geometry_normal);
			ray.t_max = isinf(t_max) ? t_max : ray.t_max - dt;
			continue;
		} else {
			// hit a surface
			transmittance = 0;
			dir_pdf = 0;
			nee_pdf = 0;
			break;
		}
	}
}

template<typename Material>
inline void reservoir_ris(const Material material, const uint index_1d) {
	Reservoir r;
	init_reservoir(r);
	const float3 dir_in = -path_ray(index_1d).direction;
	for (uint i = 0; i < gPushConstants.gReservoirSamples; i++) {
		const float4 light_rnd = float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d));
		const float res_rnd = rng_next_float(index_1d);

		LightSampleRecord ls;
		sample_light_or_environment(ls, light_rnd, gPathVertex.position);
		if (ls.pdfA <= 0) continue;

		const MaterialEvalRecord f = eval_material(material, dir_in, ls.to_light, index_1d, TRANSPORT_TO_LIGHT);
		if (f.pdfW <= 0) continue;
		
		ReservoirLightSample s;
		s.instance_primitive_index = ls.instance_primitive_index;
		if (ls.instance_index() != INVALID_INSTANCE) {
			if (gInstances[ls.instance_index()].type() == INSTANCE_TYPE_TRIANGLES)
				s.position_or_bary = float3(ls.bary, 0);
			else
				s.position_or_bary = ls.position;
		}
		r.update(res_rnd, s, ls.pdfA, luminance(f.f * ls.radiance) * ls.G);
	}
	gReservoirs[index_1d] = r;
}

void sample_light(const uint index_1d, out DirectLightSample r) {
	const float3 dir_in = -path_ray(index_1d).direction;
	LightSampleRecord ls;
	sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), gPathVertex.position);
	if (ls.pdfA <= 0) { r.radiance = 0; return; }
	r.radiance = ls.radiance / ls.pdfA;
	r.G = ls.G;
	r.to_light = ls.to_light;
	
	ray_query_t rayQuery;
	float T_dir_pdf = 1;
	float T_nee_pdf = 1;
	eval_transmittance(rayQuery, index_1d, ls.to_light, ls.dist, r.radiance, T_dir_pdf, T_nee_pdf);
	if (all(r.radiance <= 0)) return;
	r.radiance /= T_nee_pdf;
	r.pdfs = pack_f16_2(float2(ls.pdfA*T_nee_pdf, T_dir_pdf));
}

void sample_reservoir(const uint index_1d, out DirectLightSample r) {
	float3 pos = gReservoirs[index_1d].light_sample.position_or_bary;
	if (gReservoirs[index_1d].light_sample.instance_index() != INVALID_INSTANCE)
		if (gInstances[gReservoirs[index_1d].light_sample.instance_index()].type() == INSTANCE_TYPE_SPHERE)
			pos = gInstances[gReservoirs[index_1d].light_sample.instance_index()].inv_transform.transform_point(pos);

	PathVertexGeometry light_vertex;
	instance_geometry(light_vertex, gReservoirs[index_1d].light_sample.instance_primitive_index, pos, gReservoirs[index_1d].light_sample.position_or_bary.xy);

	r.radiance = light_emission(gReservoirs[index_1d].light_sample.instance_primitive_index, light_vertex) * gReservoirs[index_1d].W();

	float pdf, pdfA, dist;
	light_sample_pdf(gReservoirs[index_1d].light_sample.instance_index(), light_vertex, gPathVertex.position, r.to_light, dist, pdf, pdfA, r.G);
	if (pdfA <= 0) { r.radiance = 0; return; }
	
	float T_dir_pdf = 1;
	float T_nee_pdf = 1;
	ray_query_t rayQuery;
	eval_transmittance(rayQuery, index_1d, r.to_light, dist, r.radiance, T_dir_pdf, T_nee_pdf);
	if (all(r.radiance <= 0)) return;
	r.radiance /= T_nee_pdf;
	
	r.pdfs = pack_f16_2(float2(pdfA*T_nee_pdf, T_dir_pdf));
}

void store_light_vertex(const uint index_1d) {
	LightPathVertex v;
	v.throughput = gPathState.throughput;
	v.instance_primitive_index = gPathState.instance_primitive_index;
	v.pdfA = gPathState.pdfA;
	//v.vertex = vertex;
	gPathVertices[v.vertex] = gPathVertex;
}

template<typename Material>
void _sample_material_and_eval_light(const uint address, const uint index_1d, const uint2 output_index, const DirectLightSample ls, const LightPathVertex lv) {
	const float3 dir_in = -path_ray(index_1d).direction;
	const float3 rnd = float3(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d));

	const Material material = load_material<Material>(address + 4, index_1d);
	gMaterialSamples[index_1d] = sample_material(material, rnd, dir_in, index_1d, gTraceLightPaths ? TRANSPORT_FROM_LIGHT : TRANSPORT_TO_LIGHT);

	if (any(ls.radiance > 0)) {
		const MaterialEvalRecord f = eval_material(material, dir_in, ls.to_light, index_1d, gTraceLightPaths ? TRANSPORT_FROM_LIGHT : TRANSPORT_TO_LIGHT);
		if (f.pdfW > 0) {
			const float mis_w = (gSamplingFlags & SAMPLE_FLAG_DIRECT_ONLY) ? 1 : 
				gUseMIS ? mis_heuristic(ls.pdfA(), pdfWtoA(f.pdfW, ls.G)*ls.T_dir_pdf()) : 0.5;
			gRadiance[output_index].rgb += gPathState.throughput * f.f * ls.radiance * ls.G * mis_w;
		}
	}

	if (any(lv.throughput > 0)) {
		// TODO: add light subpath contribution
		//gRadiance[output_index].rgb += gPathState.throughput * lv.throughput * f.f * G * mis_w;
	}
}
void sample_material_and_eval_light(const uint index_1d, const uint2 output_index, const DirectLightSample ls, const LightPathVertex lv) {
	const uint address = path_material_address(index_1d);
	const uint type = gMaterialData.Load(address);
	switch (type) {
	#define CASE_FN(Material) case e##Material: _sample_material_and_eval_light<Material>(address, index_1d, output_index, ls, lv); break;
	FOR_EACH_BSDF_TYPE( CASE_FN )
	#undef CASE_FN
	}
}

bool trace_material_sample(inout ray_query_t rayQuery, const uint index_1d, const uint2 output_index) {
	if (gMaterialSamples[index_1d].eval.pdfW <= 0)
		return false;
	gPathState.throughput *= gMaterialSamples[index_1d].eval.f / gMaterialSamples[index_1d].eval.pdfW;

	const float ndotout = dot(gMaterialSamples[index_1d].dir_out, gPathVertex.geometry_normal);

	RayDifferential ray_out;
	ray_out.origin = gPathVertex.shape_area == 0 ? gPathVertex.position : ray_offset(gPathVertex.position, ndotout < 0 ? -gPathVertex.geometry_normal : gPathVertex.geometry_normal);
	ray_out.t_min = 0;
	ray_out.direction = gMaterialSamples[index_1d].dir_out;
	ray_out.t_max = 1.#INF;

	const RayDifferential ray_in = path_ray(index_1d);
	ray_out.radius = gPathState.instance_index() == INVALID_INSTANCE ? ray_in.radius : ray_in.differential_transfer(length(gPathVertex.position - ray_in.origin));
	if (gMaterialSamples[index_1d].eta() == 0) {
		ray_out.spread = ray_in.differential_reflect(gPathVertex.mean_curvature, gMaterialSamples[index_1d].roughness());
	} else if (gMaterialSamples[index_1d].eta() > 0) {
		ray_out.spread = ray_in.differential_refract(gPathVertex.mean_curvature, gMaterialSamples[index_1d].roughness(), gMaterialSamples[index_1d].eta());
		gPathState.eta_scale /= pow2(gMaterialSamples[index_1d].eta());
	} else
		ray_out.spread = ray_in.spread;
	
	gPathState.ray_origin = ray_out.origin;
	gPathState.radius_spread = pack_f16_2(float2(ray_out.radius, ray_out.spread));	

	float transmit_dir_pdf, transmit_nee_pdf;
	intersect_and_scatter(rayQuery, ray_out, index_1d, transmit_dir_pdf, transmit_nee_pdf);


	const uint material_address = path_material_address(index_1d);
	if (material_address == INVALID_MATERIAL)
		return false;

	if (gTraceLightPaths) {
		if (gPathState.instance_index() == INVALID_INSTANCE) {
			gPathState.pdfA *= gMaterialSamples[index_1d].eval.pdfW;
		} else {
			const float3 to_vertex = gPathVertex.position - gPathState.ray_origin;
			gPathState.pdfA *= pdfWtoA(gMaterialSamples[index_1d].eval.pdfW, abs(ndotout) / dot(to_vertex, to_vertex));
		}
		store_light_vertex(index_1d);
	} else {
		const float3 L = load_material_and_eval_emission(material_address, -ray_out.direction, index_1d).f;
		if (any(L > 0)) {
			// hit a light
			float w = 1;
			uint instance_index = gPathState.instance_index();
			if ((gSampleEnvironment && instance_index == INVALID_INSTANCE) || (gSampleEmissive && instance_index != INVALID_INSTANCE)) {
				w = 0.5;
				if (gUseMIS) {
					float pdf, pdfA, G, dist;
					float3 to_light;
					light_sample_pdf(instance_index, gPathVertex, gPathState.ray_origin, to_light, dist, pdf, pdfA, G);
					if (pdf > 0)
						w = mis_heuristic(pdfWtoA(gMaterialSamples[index_1d].eval.pdfW, G) * transmit_dir_pdf, pdfA * transmit_nee_pdf);
				}
			}
			gRadiance[output_index].rgb += gPathState.throughput * L * w;
		}
	}

	return true;
}

[numthreads(8, 8, 1)]
void sample_light_paths(uint3 index : SV_DispatchThreadID) {
	uint w,h;
	gRadiance.GetDimensions(w,h);
	if (index.x >= w || index.y >= h) return;

	const uint index_1d = index.y*w + index.x;

	gPathState.rng_state = uint4(index_1d/64, index_1d%64, gPushConstants.gRandomSeed, 0);

	LightSampleRecord ls;
	sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), 0);

	gPathState.throughput = ls.radiance;
	gPathState.pdfA = ls.pdfA;

	RayDifferential ray;
	ray.t_min = 0;
	ray.t_max = 1.#INF;
	if (ls.instance_index() == INVALID_INSTANCE) {
		ray.origin = ls.position*1e6;
		ray.direction = ls.position;
	} else {
		ShadingFrame frame;
		frame.n = ls.normal;
		make_orthonormal(frame.n, frame.t, frame.b);
		const float3 local_dir_out = sample_cos_hemisphere(rng_next_float(index_1d), rng_next_float(index_1d));
		ray.origin = ls.position;
		ray.direction = frame.to_world(local_dir_out);
		gPathState.pdfA *= cosine_hemisphere_pdfW(local_dir_out.z);
	}
	
	gPathState.ray_origin = ray.origin;
	gPathState.radius_spread = 0;
	gPathState.vol_index = -1;
	gPathState.eta_scale = 1;
	
	ray_query_t rayQuery;

	float dir_pdf, nee_pdf;
	intersect_and_scatter(rayQuery, ray, index_1d, dir_pdf, nee_pdf);

	if (gPathState.instance_index() == INVALID_INSTANCE)
		gPathState.throughput = 0;
}

[numthreads(8, 8, 1)]
void sample_visibility(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	const float2 uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	const RayDifferential view_ray = gViews[view_index].create_ray(uv, gViews[view_index].extent());

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	gPathState.rng_state = uint4(index.xy, gPushConstants.gRandomSeed, 0);

	gPathState.ray_origin = view_ray.origin;
	gPathState.radius_spread = pack_f16_2(float2(view_ray.radius, view_ray.spread));
	gPathState.vol_index = gViewVolumeInstances[view_index];
	gPathState.throughput = 1;
	gPathState.eta_scale = 1;
	gPathState.pdfA = 1;

	ray_query_t rayQuery;
	float dir_pdf, nee_pdf;
	intersect_and_scatter(rayQuery, view_ray, index_1d, dir_pdf, nee_pdf);

	 // store visibility
	gVisibility[0][index.xy] = gPathState.rng_state;
	gVisibility[1][index.xy] = uint4(gPathState.instance_primitive_index, asuint(rayQuery.CommittedTriangleBarycentrics()), pack_normal_octahedron(gPathVertex.shading_normal));

	float2 prev_uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float prev_z = 1.#INF;
	float2 dz = 0;

	if (gPathState.instance_index() != INVALID_INSTANCE) {
		switch (gInstances[gPathState.instance_index()].type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			// TODO: figure out dz
			const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(gPathVertex.geometry_normal);
			dz = 1/(abs(view_normal.xy) + 1e-2);
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			dz = 1/sqrt(gInstances[gPathState.instance_index()].radius());
			break;
		case INSTANCE_TYPE_VOLUME:
			dz = 1;
			break;
		}
		
		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, gInstances[gPathState.instance_index()].prev_transform).transform_point(gPathVertex.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	}

	const float z = gPathState.instance_index() == INVALID_INSTANCE ? 1.#INF : length(gPathState.ray_origin - gPathVertex.position);	
	gVisibility[2][index.xy] = uint4(pack_f16_2(float2(z, prev_z)), pack_f16_2(dz), asuint(prev_uv));
	
	// primary albedo and emission
	const uint material_address = path_material_address(index_1d);
	if (material_address == INVALID_MATERIAL) {
		gRadiance[index.xy] = 0;
		gAlbedo[index.xy] = 0;
		gPathState.throughput = 0;
		return;
	}

	const uint type = gMaterialData.Load(material_address);
	switch (type) {
	#define CASE_FN(Material) \
	case e##Material: {\
		const Material material = load_material<Material>(material_address+4, index_1d); \
		gRadiance[index.xy] = float4(gPathState.throughput * eval_material_emission(material, -view_ray.direction, index_1d).f, 1); \
		gAlbedo  [index.xy] = float4(gPathState.throughput *   eval_material_albedo(material, index_1d), 1); \
		if (gSampleReservoirs && (gSampleEnvironment || gSampleEmissive) && gPathState.instance_index() != INVALID_INSTANCE) \
			reservoir_ris(material, index_1d); \
		break; \
	}
	FOR_EACH_BSDF_TYPE(CASE_FN);
	#undef CASE_FN
	}

	if (gPathState.instance_index() == INVALID_INSTANCE)
		gPathState.throughput = 0;
	
	switch (gDebugMode) {
		default:
			break;
		case DebugMode::eZ:
			gRadiance[index.xy].rgb = viridis_quintic(1 - exp(-0.1*z));
			gPathState.throughput = 0;
			break;
		case DebugMode::eDz:
			gRadiance[index.xy].rgb = viridis_quintic(length(dz));
			gPathState.throughput = 0;
			break;
		case DebugMode::eShadingNormal:
			gRadiance[index.xy].rgb = gPathVertex.shading_normal*.5 + .5;
			gPathState.throughput = 0;
			break;
		case DebugMode::eGeometryNormal:
			gRadiance[index.xy].rgb = gPathVertex.geometry_normal*.5 + .5;
			gPathState.throughput = 0;
			break;
		case DebugMode::eMaterialID: {
			const uint material_address = path_material_address(index_1d);
			if (material_address == INVALID_MATERIAL)
				gRadiance[index.xy].rgb = 0;
			else {
				static const float3 colors[] = {
					float3(1,0,0),
					float3(0,1,0),
					float3(0,0,1),
					float3(0,1,1),
					float3(1,1,0),
					float3(1,0,1),
					float3(1,1,1),
				};
				gRadiance[index.xy].rgb = colors[gMaterialData.Load(material_address) % 7];
			}
			gPathState.throughput = 0;
			break;
		}
		case DebugMode::eTangent:
			gRadiance[index.xy].rgb = gPathVertex.tangent.xyz*.5 + .5;
			gPathState.throughput = 0;
			break;
		case DebugMode::eMeanCurvature:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(gPathVertex.mean_curvature));
			gPathState.throughput = 0;
			break;
		case DebugMode::eRayRadius:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(100*gPathState.radius()));
			gPathState.throughput = 0;
			break;
		case DebugMode::eUVScreenSize:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(log2(1024*gPathVertex.uv_screen_size)/10));
			gPathState.throughput = 0;
			break;
		case DebugMode::ePrevUV:
			gRadiance[index.xy].rgb = float3(prev_uv, 0);
			gPathState.throughput = 0;
			break;
	}
}

[numthreads(8, 8, 1)]
void integrate_indirect(uint3 index : SV_DispatchThreadID) {
	uint w,h;
	gRadiance.GetDimensions(w,h);
	if (index.x >= w || index.y >= h) return;

	const uint index_1d = index.y*w + index.x;
	
	for (uint depth = 1; depth < gPushConstants.gMaxDepth-1; depth++) {
		if (all(gPathState.throughput <= 1e-6)) break;
		
		// light sampling
		DirectLightSample ls;
		ls.radiance = 0;
		ls.pdfs = 0;
		if (!gTraceLightPaths && (gSampleEmissive || gSampleEnvironment)) {
			if (gSampleReservoirs && depth == 1)
				sample_reservoir(index_1d, ls);
			else
				sample_light(index_1d, ls);
		}

		LightPathVertex lv;
		lv.throughput = 0;
		lv.pdfA = 0;
		if (gSampleLightPaths) {
			// TODO: sample lv
		}

		// stores material sample in gMaterialSamples[index_1d]
		sample_material_and_eval_light(index_1d, index.xy, ls, lv);

		if (gSamplingFlags & SAMPLE_FLAG_DIRECT_ONLY) break;

		ray_query_t rayQuery;
		if (!trace_material_sample(rayQuery, index_1d, index.xy)) break;

		if (gPathState.instance_index() == INVALID_INSTANCE) break;
		
		if (depth >= gPushConstants.gMinDepth) {
			const float l = min(max3(gPathState.throughput) / gPathState.eta_scale, 0.95);
			if (rng_next_float(index_1d) > l)
				break;
			else
				gPathState.throughput /= l;
		}
	}
}