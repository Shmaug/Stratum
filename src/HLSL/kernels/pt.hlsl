#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_photons
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E random_walk
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E resolve

#define PT_DESCRIPTOR_SET_0
#define PT_DESCRIPTOR_SET_1
#include "../pt_descriptors.hlsli"

[[vk::constant_id(0)]] const uint gDebugMode = 0;
[[vk::constant_id(1)]] const uint gSamplingFlags = 0x11;

#define gSampleEnvironment		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_ENVIRONMENT)
#define gSampleEmissive 		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_EMISSIVE)
#define gTraceLightPaths		(gSamplingFlags & SAMPLE_FLAG_TRACE_LIGHT_PATHS)
#define gStorePathVertices		(gSamplingFlags & SAMPLE_FLAG_STORE_PATH_VERTICES)
#define gUniformSphereSampling 	(gSamplingFlags & SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING)
#define gSampleReservoirs		(gSamplingFlags & SAMPLE_FLAG_SAMPLE_RESERVOIRS)
#define gUseMIS 				(gSamplingFlags & SAMPLE_FLAG_MIS)
#define gUseRayCones 			(gSamplingFlags & SAMPLE_FLAG_RAY_CONE_LOD)
#define gEnableVolumes 			(gSamplingFlags & SAMPLE_FLAG_ENABLE_VOLUMES)

[[vk::push_constant]] const PathTracePushConstants gPushConstants;

#define ray_query_t RayQuery<RAY_FLAG_NONE>
#define gPathState gPathStates[index_1d]
#define gPathVertex gPathStateVertices[index_1d]
#define gPathShadingData gPathStateShadingData[index_1d]

#define PNANOVDB_HLSL
#include "../../extern/nanovdb/PNanoVDB.h"
#include "../tonemap.hlsli"
#include "../rng.hlsli"
#include "../image_value.hlsli"
#include "../material.hlsli"
#include "../light.hlsli"

bool is_volume(const uint instance_index) {
	const uint material_address = instance_index == INVALID_INSTANCE ? gPushConstants.gEnvironmentMaterialAddress : gInstances[instance_index].material_address();
	if (material_address == -1) return false;
	const uint type = gMaterialData.Load(material_address);
	return type == BSDFType::eHeterogeneousVolume;
}
uint instance_material_address(const uint instance_index) {
	if (instance_index == INVALID_INSTANCE)
		return gPushConstants.gEnvironmentMaterialAddress;
	else
		return gInstances[instance_index].material_address();
}
float3 path_dir_in(const uint index_1d) { return normalize(gPathShadingData.position - gPathState.prev_vertex_position); }
float3 path_ray_origin(const uint index_1d, const float3 direction) {
	if (gPathShadingData.shape_area == 0)
		return gPathShadingData.position;
	else {
		const float3 ng = gPathShadingData.geometry_normal();
		const bool inside = dot(direction, ng) < 0;
		return ray_offset(gPathShadingData.position, inside ? -ng : ng);
	}
}

bool do_ray_query(inout ray_query_t rayQuery, const float3 origin, const float3 direction, const float t_max, out float3 vol_normal) {
	gCounters.InterlockedAdd(COUNTER_ADDRESS_RAY_COUNT, 1);
	RayDesc rayDesc;
	rayDesc.Origin = origin;
	rayDesc.Direction = direction;
	rayDesc.TMin = 0;
	rayDesc.TMax = t_max;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, rayDesc);
	while (rayQuery.Proceed()) {
		switch (rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE: {
				const InstanceData instance = gInstances[rayQuery.CandidateInstanceID()];
				switch (instance.type()) {
					case INSTANCE_TYPE_SPHERE: {
						const float2 st = ray_sphere(rayQuery.CandidateObjectRayOrigin(), rayQuery.CandidateObjectRayDirection(), 0, instance.radius());
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
void intersect(inout ray_query_t rayQuery, const float3 origin, const float3 direction, const float t_max, out ShadingData v, out uint instance_primitive_index) {
	float3 vol_normal;
	if (do_ray_query(rayQuery, origin, direction, t_max, vol_normal)) {
		// hit an instance
		BF_SET(instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
			const float3 local_pos = rayQuery.CommittedObjectRayOrigin() + rayQuery.CommittedObjectRayDirection()*rayQuery.CommittedRayT();
			switch (gInstances[rayQuery.CommittedInstanceID()].type()) {
				case INSTANCE_TYPE_SPHERE:
					make_sphere_shading_data(v, rayQuery.CommittedInstanceID(), local_pos);
					break;
				case INSTANCE_TYPE_VOLUME:
					make_volume_shading_data(v, rayQuery.CommittedInstanceID(), local_pos);
					v.packed_geometry_normal = v.packed_shading_normal = pack_normal_octahedron(vol_normal);
					break;
			}
		} else {
			// triangle
			BF_SET(instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			make_triangle_shading_data_from_barycentrics(v, rayQuery.CommittedInstanceID(), rayQuery.CommittedPrimitiveIndex(), rayQuery.CommittedTriangleBarycentrics());
		}
		v.flags = 0;
		if (dot(direction, v.geometry_normal()) < 0)
			v.flags |= SHADING_FLAG_FRONT_FACE;
	} else {
		// background
		BF_SET(instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		v.position = direction;
		v.shape_area = 0;
		v.uv = cartesian_to_spherical_uv(direction);
		v.mean_curvature = 1;
		v.uv_screen_size = 0;
	}
}
void path_eval_transmittance(inout ray_query_t rayQuery, const uint index_1d, const float3 direction, float t_max, inout float3 transmittance, inout float dir_pdf, inout float nee_pdf) {
	uint cur_vol_instance = gPathVertex.vol_index;

	float3 origin = path_ray_origin(index_1d, direction);
	t_max *= 0.999;
	while (t_max > 1e-6f) {
		ShadingData v;
		uint instance_primitive_index;
		intersect(rayQuery, origin, direction, t_max, v, instance_primitive_index);

		if (!gEnableVolumes) {
			if (BF_GET(instance_primitive_index,0,16) != INVALID_INSTANCE) {
				// hit a surface
				transmittance = 0;
				dir_pdf = 0;
				nee_pdf = 0;
			}
			break;
		}

		const float dt = (BF_GET(instance_primitive_index, 0, 16) == INVALID_INSTANCE) ? t_max : length(v.position - origin);

		if (is_volume(cur_vol_instance)) {
			// interact with volume
			const HeterogeneousVolume::DeltaTrackResult vt = load_material<HeterogeneousVolume>(gInstances[cur_vol_instance].material_address() + 4, v).delta_track(
				gInstances[cur_vol_instance].inv_transform.transform_point(origin),
				gInstances[cur_vol_instance].inv_transform.transform_vector(direction),
				dt, index_1d, false);
			transmittance *= vt.transmittance;
			dir_pdf *= average(vt.dir_pdf);
			nee_pdf *= average(vt.nee_pdf);
		}

		if (BF_GET(instance_primitive_index,0,16) == INVALID_INSTANCE) break;

		if (is_volume(BF_GET(instance_primitive_index,0,16))) {
			if (v.flags & SHADING_FLAG_FRONT_FACE) {
				// entering volume
				cur_vol_instance = BF_GET(instance_primitive_index,0,16);
				origin = ray_offset(v.position, -v.geometry_normal());
			} else {
				// leaving volume
				cur_vol_instance = INVALID_INSTANCE;
				origin = ray_offset(v.position, v.geometry_normal());
			}
			if (!isinf(t_max))
				t_max -= dt;
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
void path_state_trace_bounce(inout ray_query_t rayQuery, const uint index_1d, out float transmit_dir_pdf, out float transmit_nee_pdf) {
	gPathState.prev_vertex_position = path_ray_origin(index_1d, gPathState.dir_out);
	gPathState.prev_vertex_packed_geometry_normal = gPathShadingData.packed_geometry_normal;

	transmit_dir_pdf = 1;
	transmit_nee_pdf = 1;
	float3 origin = gPathState.prev_vertex_position;
	for (uint steps = 0; steps < 64; steps++) {
		intersect(rayQuery, origin, gPathState.dir_out, 1.#INF, gPathShadingData, gPathVertex.instance_primitive_index);

		if (!gEnableVolumes) break;

		if (is_volume(gPathVertex.vol_index)) {
			// interact with volume
			ShadingData sd;
			const HeterogeneousVolume::DeltaTrackResult tr = load_material<HeterogeneousVolume>(gInstances[gPathVertex.vol_index].material_address() + 4, sd).delta_track(
				gInstances[gPathVertex.vol_index].inv_transform.transform_point(origin),
				gInstances[gPathVertex.vol_index].inv_transform.transform_vector(gPathState.dir_out),
				rayQuery.CommittedRayT(), index_1d);
			gPathVertex.beta *= tr.transmittance / average(tr.dir_pdf);
			transmit_dir_pdf *= average(tr.dir_pdf);
			transmit_nee_pdf *= average(tr.nee_pdf);
			if (all(isfinite(tr.scatter_p))) {
				uint instance_primitive_index = 0;
				BF_SET(instance_primitive_index, gPathVertex.vol_index, 0, 16);
				BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				gPathVertex.instance_primitive_index = instance_primitive_index;
				gPathShadingData.position = tr.scatter_p;
				gPathShadingData.shape_area = 0;
				break;
			}
		}

		if (gPathVertex.instance_index() != INVALID_INSTANCE && is_volume(gPathVertex.instance_index())) {
			if (gPathShadingData.flags & SHADING_FLAG_FRONT_FACE) {
				// entering volume
				gPathVertex.vol_index = gPathVertex.instance_index();
				origin = ray_offset(gPathShadingData.position, -gPathShadingData.geometry_normal());
			} else {
				// leaving volume
				gPathVertex.vol_index = INVALID_INSTANCE;
				origin = ray_offset(gPathShadingData.position, gPathShadingData.geometry_normal());
			}
		} else
			break;
	}

	if (gPathVertex.instance_index() != INVALID_INSTANCE)
		gPathState.ray_differential.transfer(length(gPathState.prev_vertex_position - gPathShadingData.position));

	gPathShadingData.uv_screen_size *= gPathState.ray_differential.radius;
}

#include "../bdpt_connect.hlsli"

template<typename Material>
void reservoir_ris(const Material material, const uint index_1d) {
	Reservoir r;
	init_reservoir(r);
	const float3 dir_in = -path_dir_in(index_1d);
	for (uint i = 0; i < gPushConstants.gReservoirSamples; i++) {
		const float4 light_rnd = float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d));
		const float res_rnd = rng_next_float(index_1d);

		LightSampleRecord ls;
		sample_light_or_environment(ls, light_rnd, gPathShadingData.position);
		if (ls.pdfA <= 0) continue;

		const MaterialEvalRecord f = eval_material(material, dir_in, ls.to_light, gPathShadingData, false);
		if (f.pdf_fwd <= 0) continue;

		const float l = luminance(f.f * ls.radiance);

		ReservoirLightSample s;
		s.instance_primitive_index = ls.instance_primitive_index;
		if (ls.instance_index() == INVALID_INSTANCE) {
			s.local_position = ls.position;
			r.update(res_rnd, s, ls.pdfA, l);
		} else {
			s.local_position = gInstances[ls.instance_index()].inv_transform.transform_point(ls.position);
			r.update(res_rnd, s, ls.pdfA, l * abs(dot(ls.to_light, ls.normal)) / pow2(ls.dist));
		}
	}
	gReservoirs[index_1d] = r;
}

struct DirectLightSample {
	float3 radiance;
	float G;
	float3 to_light;
	float pdfA;
	float T_dir_pdf;
};
void sample_reservoir(inout ray_query_t rayQuery, const uint index_1d, out DirectLightSample r) {
	ShadingData light_vertex;
	make_shading_data(light_vertex, gReservoirs[index_1d].light_sample.instance_primitive_index, gReservoirs[index_1d].light_sample.local_position);

	float pdfA, dist;
	light_sample_pdf(gReservoirs[index_1d].light_sample.instance_index(), light_vertex, gPathShadingData.position, r.to_light, dist, pdfA, r.G);
	if (pdfA <= 0) { r.radiance = 0; return; }

	r.radiance = light_emission(gReservoirs[index_1d].light_sample.instance_primitive_index, -r.to_light, light_vertex) * gReservoirs[index_1d].W();

	r.T_dir_pdf = 1;
	float T_nee_pdf = 1;
	path_eval_transmittance(rayQuery, index_1d, r.to_light, dist, r.radiance, r.T_dir_pdf, T_nee_pdf);
	if (all(r.radiance <= 0)) return;
	r.radiance /= T_nee_pdf;
	r.pdfA = pdfA*T_nee_pdf;
}
void sample_light(inout ray_query_t rayQuery, const uint index_1d, out DirectLightSample r) {
	const float3 dir_in = -path_dir_in(index_1d);
	LightSampleRecord ls;
	sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), gPathShadingData.position);
	if (ls.pdfA <= 0) { r.radiance = 0; return; }
	r.radiance = ls.radiance / ls.pdfA;
	if (ls.instance_index() == INVALID_INSTANCE)
		r.G = 1;
	else
		r.G = abs(dot(ls.normal, ls.to_light)) / pow2(ls.dist);
	r.to_light = ls.to_light;

	r.T_dir_pdf = 1;
	float T_nee_pdf = 1;
	path_eval_transmittance(rayQuery, index_1d, ls.to_light, ls.dist, r.radiance, r.T_dir_pdf, T_nee_pdf);
	if (all(r.radiance <= 0)) return;
	r.radiance /= T_nee_pdf;
	r.pdfA = ls.pdfA*T_nee_pdf;
}

template<typename Material>
void path_direct_light(const Material material, inout ray_query_t rayQuery, const uint index_1d, const uint2 output_index, const float3 dir_in, const uint depth) {
	DirectLightSample ls;
	ls.radiance = 0;
	ls.pdfA = 0;
	if (gSampleReservoirs && depth == 1)
		sample_reservoir(rayQuery, index_1d, ls);
	else
		sample_light(rayQuery, index_1d, ls);

	if (any(ls.radiance > 0)) {
		const MaterialEvalRecord f = eval_material(material, dir_in, ls.to_light, gPathShadingData, false);
		if (f.pdf_fwd > 0) {
			float w;
			if (gSamplingFlags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
				w = path_weight(depth+1, 1);
			else
				w = gUseMIS ? mis_heuristic(ls.pdfA, pdfWtoA(f.pdf_fwd, ls.G)*ls.T_dir_pdf) : 0.5;
			gRadiance[output_index].rgb += gPathVertex.beta * f.f * ls.radiance * ls.G * w;
		}
	}
}

template<typename Material>
void path_bounce_material(inout ray_query_t rayQuery, const uint address, const uint index_1d, const uint2 output_index, const uint depth, out MaterialSampleRecord s) {
	const float3 rnd = float3(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d));
	const float3 dir_in = -path_dir_in(index_1d);
	const Material material = load_material<Material>(address + 4, gPathShadingData);

	if (gTraceLightPaths)
		connect_light_path_to_view(material, rayQuery, index_1d, dir_in, depth);
	else if (gSampleEmissive || gSampleEnvironment) {
		if (gSamplingFlags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
			connect_to_light_paths(material, rayQuery, index_1d, output_index, dir_in, depth);
		path_direct_light(material, rayQuery, index_1d, output_index, dir_in, depth);
	}

	// sample material & apply contribution
	s = sample_material(material, rnd, dir_in, gPathShadingData, gTraceLightPaths);
}
void path_bounce(inout ray_query_t rayQuery, const uint index_1d, const uint2 output_index, const uint depth) {
	// store light path vertices
	if (gTraceLightPaths) {
		// convert pdf_fwd (computed at previous vertex) to area measure
		if (gPathVertex.instance_index() != INVALID_INSTANCE) {
			const float3 dir_in = gPathState.prev_vertex_position - gPathShadingData.position;
			const float dist2 = dot(dir_in, dir_in);
			float G = 1 / dist2;
			if (gPathShadingData.shape_area != 0)
				G *= abs(dot(dir_in/sqrt(dist2), gPathShadingData.geometry_normal()));
			gPathVertex.pdf_fwd = pdfWtoA(gPathVertex.pdf_fwd, G);
		}
		gPathVertex.pdf_rev = 0; // pdf_rev is assigned next iteration
		gLightPathShadingData[gPushConstants.gNumLightPaths*depth + index_1d] = gPathShadingData;
		gLightPathVertices[gPushConstants.gNumLightPaths*depth + index_1d] = gPathVertex;
		gLightPathVertices[index_1d].path_length++;
	}

	MaterialSampleRecord s;

	const uint address = instance_material_address(gPathVertex.instance_index());
	const uint type = gMaterialData.Load(address);
	switch (type) {
	#define CASE_FN(Material) case e##Material: if (material_has_bsdf<Material>()) path_bounce_material<Material>(rayQuery, address, index_1d, output_index, depth, s); break;
	FOR_EACH_BSDF_TYPE( CASE_FN )
	#undef CASE_FN
	}

	if (s.eta == 0) {
		gPathState.ray_differential.reflect(gPathShadingData.mean_curvature, s.roughness);
	} else if (s.eta > 0) {
		gPathState.ray_differential.refract(gPathShadingData.mean_curvature, s.roughness, s.eta);
		gPathState.eta_scale /= pow2(s.eta);
	}

	gPathVertex.beta *= s.f / s.pdf_fwd;
	gPathVertex.pdf_fwd = s.pdf_fwd;
	gPathVertex.pdf_rev = s.pdf_rev;
	gPathState.dir_out = s.dir_out;
	gPathState.dir_out_pdfW = s.pdf_fwd;

	// compute reverse probability at previous vertex
	if (gTraceLightPaths) {
		if (gPathVertex.instance_index() != INVALID_INSTANCE) {
			const float3 dir = gPathShadingData.position - gPathState.prev_vertex_position;
			const float dist2 = dot(dir, dir);
			float G = 1 / dist2;
			if (gPathShadingData.shape_area != 0)
				G *= abs(dot(dir/sqrt(dist2), gPathState.prev_vertex_geometry_normal()));
			gLightPathVertices[gPushConstants.gNumLightPaths*(depth-1) + index_1d].pdf_rev = pdfWtoA(s.pdf_rev, G);
		}
	}
}

[numthreads(8, 4, 1)]
void sample_visibility(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	gPathState.rng_state = uint4(index.xy, gPushConstants.gRandomSeed, 0);
	gPathState.radiance_mutex = 0;

	gPathShadingData.position = gViews[view_index].camera_to_world.transform_point(0);
	gPathShadingData.packed_geometry_normal = pack_normal_octahedron(normalize(gViews[view_index].camera_to_world.transform_vector(float3(0,0,sign(gViews[view_index].projection.near_plane)))));
	gPathVertex.vol_index = gViewVolumeInstances[view_index];
	gPathVertex.beta = 1;
	gPathVertex.pdf_fwd = 0;
	gPathVertex.pdf_rev = 0;

	const float2 jitter = (gSamplingFlags & SAMPLE_FLAG_SAMPLE_PIXEL_AREA) ? float2(rng_next_float(index_1d), rng_next_float(index_1d)) : 0.5;
	const float2 uv = (index.xy + jitter - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_ray_dir = normalize(gViews[view_index].projection.back_project(clipPos));
	gPathState.dir_out = normalize(gViews[view_index].camera_to_world.transform_vector(local_ray_dir));
	gPathState.dir_out_pdfW = 1;
	gPathState.eta_scale = 1;
	gPathState.ray_differential.radius = 0;
	gPathState.ray_differential.spread = 1 / min(gViews[view_index].extent().x, gViews[view_index].extent().y);

	ray_query_t rayQuery;
	float dir_pdf, nee_pdf;
	path_state_trace_bounce(rayQuery, index_1d, dir_pdf, nee_pdf);

	// store visibility
	float2 prev_uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float prev_z = 1.#INF;
	float2 dz = 0;
	if (gPathVertex.instance_index() != INVALID_INSTANCE) {
		switch (gInstances[gPathVertex.instance_index()].type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			// TODO: figure out dz
			const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(gPathShadingData.geometry_normal());
			dz = 1/(abs(view_normal.xy) + 1e-2);
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			dz = 1/sqrt(gInstances[gPathVertex.instance_index()].radius());
			break;
		case INSTANCE_TYPE_VOLUME:
			dz = 1;
			break;
		}

		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, gInstances[gPathVertex.instance_index()].motion_transform).transform_point(gPathShadingData.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevScreenPos.xyz /= prevScreenPos.w;
		prev_uv = prevScreenPos.xy*.5 + .5;
	}
	const float z = gPathVertex.instance_index() == INVALID_INSTANCE ? 1.#INF : length(gPathState.prev_vertex_position - gPathShadingData.position);
	gVisibility[index_1d].rng_state = gPathState.rng_state;
	gVisibility[index_1d].position = gPathShadingData.position;
	gVisibility[index_1d].instance_primitive_index = gPathVertex.instance_primitive_index;
	gVisibility[index_1d].store_nz(gPathShadingData.shading_normal(), z, prev_z, dz);
	gVisibility[index_1d].prev_uv = prev_uv;

	// primary albedo and emission
	const uint material_address = instance_material_address(gPathVertex.instance_index());
	if (material_address == INVALID_MATERIAL) {
		gAlbedo[index.xy] = 0;
		gPathVertex.beta = 0;
		return;
	}

	gRadiance[index.xy].w = 1;

	// check for BSDF, add emission
	bool has_bsdf = false;
	const uint type = gMaterialData.Load(material_address);
	switch (type) {
	#define CASE_FN(Material) \
	case e##Material: {\
		const Material material = load_material<Material>(material_address+4, gPathShadingData); \
		if (gPushConstants.gMaxEyeDepth > 1) \
			gRadiance[index.xy].rgb += gPathVertex.beta * eval_material_emission(material, -gPathState.dir_out, gPathShadingData).f; \
		gAlbedo[index.xy]  = float4(gPathVertex.beta * eval_material_albedo(material, gPathShadingData), 1); \
		has_bsdf = material_has_bsdf<Material>(); \
		if (gSampleReservoirs && (gSampleEnvironment || gSampleEmissive) && has_bsdf) \
			reservoir_ris(material, index_1d); \
		break; \
	}
	FOR_EACH_BSDF_TYPE(CASE_FN);
	#undef CASE_FN
	}

	if (!has_bsdf)
		gPathVertex.beta = 0;
	switch (gDebugMode) {
		default:
			break;
		case DebugMode::eZ:
			gRadiance[index.xy].rgb = viridis_quintic(1 - exp(-0.1*z));
			gPathVertex.beta = 0;
			break;
		case DebugMode::eDz:
			gRadiance[index.xy].rgb = viridis_quintic(length(dz));
			gPathVertex.beta = 0;
			break;
		case DebugMode::eShadingNormal:
			gRadiance[index.xy].rgb = gPathShadingData.shading_normal()*.5 + .5;
			gPathVertex.beta = 0;
			break;
		case DebugMode::eGeometryNormal:
			gRadiance[index.xy].rgb = gPathShadingData.geometry_normal()*.5 + .5;
			gPathVertex.beta = 0;
			break;
		case DebugMode::eMaterialID: {
			const uint material_address = instance_material_address(gPathVertex.instance_index());
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
			gPathVertex.beta = 0;
			break;
		}
		case DebugMode::eTangent:
			gRadiance[index.xy].rgb = gPathShadingData.tangent()*.5 + .5;
			gPathVertex.beta = 0;
			break;
		case DebugMode::eMeanCurvature:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(gPathShadingData.mean_curvature));
			gPathVertex.beta = 0;
			break;
		case DebugMode::eRayRadius:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(100*gPathState.ray_differential.radius));
			gPathVertex.beta = 0;
			break;
		case DebugMode::eUVScreenSize:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(log2(1024*gPathShadingData.uv_screen_size)/10));
			gPathVertex.beta = 0;
			break;
		case DebugMode::ePrevUV:
			gRadiance[index.xy].rgb = float3(prev_uv, 0);
			gPathVertex.beta = 0;
			break;
	}
}

[numthreads(8, 4, 1)]
void sample_photons(uint3 index : SV_DispatchThreadID) {
	uint w,h;
	gRadiance.GetDimensions(w,h);
	if (index.x >= w || index.y >= h) return;

	const uint index_1d = index.y*w + index.x;

	LightSampleRecord ls;
	sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), 0);

	if (ls.pdfA <= 0 || all(ls.radiance <= 0)) {
		gPathVertex.beta = 0;
		gLightPathVertices[index_1d].path_length = 0;
		return;
	}

	gPathState.ray_differential.radius = 0;
	gPathState.ray_differential.spread = 0;
	gPathState.eta_scale = 1;

	gPathShadingData.position = ls.position;
	gPathShadingData.packed_geometry_normal = pack_normal_octahedron(ls.normal);
	gPathShadingData.packed_shading_normal = gPathShadingData.packed_geometry_normal;
	gPathVertex.beta = ls.radiance/ls.pdfA;
	gPathVertex.instance_primitive_index = ls.instance_primitive_index;
	gPathVertex.vol_index = -1;
	gPathVertex.pdf_fwd = ls.pdfA;
	gPathVertex.pdf_rev = 0;
	gPathVertex.path_length = 1;
	gLightPathVertices[index_1d] = gPathVertex;
	gLightPathShadingData[index_1d] = gPathShadingData;
	gRadiance[index.xy].w = 1;

	// compute dir_out
	if (ls.instance_index() == INVALID_INSTANCE) {
		// environment sample
		gPathState.prev_vertex_position = -ls.position*1e6 + float3(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d))*100;
		gPathState.dir_out = ls.position;
		gPathState.dir_out_pdfW = ls.pdfA;
	} else {
		gPathState.prev_vertex_position = ray_offset(ls.position, ls.normal);
		float3 t,b;
		make_orthonormal(ls.normal, t, b);
		const float3 local_dir_out = sample_cos_hemisphere(rng_next_float(index_1d), rng_next_float(index_1d));
		gPathState.dir_out = local_dir_out.x * t + local_dir_out.y * b + local_dir_out.z * ls.normal;
		const float dir_pdf = cosine_hemisphere_pdfW(local_dir_out.z);
		gPathVertex.beta *= abs(local_dir_out.z) / dir_pdf;
		gPathState.dir_out_pdfW = dir_pdf;
	}

	ray_query_t rayQuery;

	float T_dir_pdf, T_nee_pdf;
	path_state_trace_bounce(rayQuery, index_1d, T_dir_pdf, T_nee_pdf);

	bool has_bsdf = false;
	const uint material_address = instance_material_address(gPathVertex.instance_index());
	if (material_address != INVALID_MATERIAL) {
		const uint type = gMaterialData.Load(material_address);
		switch (type) {
		#define CASE_FN(Material) case e##Material: has_bsdf = material_has_bsdf<Material>(); break;
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
		}
	}
	if (!has_bsdf) gPathVertex.beta = 0;
}

[numthreads(8, 4, 1)]
void random_walk(uint3 index : SV_DispatchThreadID) {
	uint w,h;
	gRadiance.GetDimensions(w,h);
	if (index.x >= w || index.y >= h) return;

	const uint index_1d = index.y*w + index.x;

	static const uint max_depth = gTraceLightPaths ? gPushConstants.gMaxLightDepth : gPushConstants.gMaxEyeDepth;

	ray_query_t rayQuery;
	uint depth;
	for (depth = 1; depth < max_depth; ) {
		if (all(gPathVertex.beta <= 1e-6)) break;

		path_bounce(rayQuery, index_1d, index.xy, depth);
		if (gPathState.dir_out_pdfW <= 0) { depth++; break; }

		float transmit_dir_pdf, transmit_nee_pdf;
		path_state_trace_bounce(rayQuery, index_1d, transmit_dir_pdf, transmit_nee_pdf);
		depth++;

		// check for bsdf and emission

		const uint material_address = instance_material_address(gPathVertex.instance_index());
		if (material_address == INVALID_MATERIAL) break;

		bool has_bsdf = false;
		float3 L = 0;
		const uint type = gMaterialData.Load(material_address);
		switch (type) {
		#define CASE_FN(Material) \
		case e##Material: {\
			has_bsdf = material_has_bsdf<Material>(); \
			const Material material = load_material<Material>(material_address+4, gPathShadingData); \
			L = eval_material_emission(material, -gPathState.dir_out, gPathShadingData).f; \
			break; \
		}
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
		}

		// add emission to pixel
		if (!gTraceLightPaths && any(L > 0)) {
			float w = 1;
			if (gSamplingFlags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
				w = path_weight(depth+1, 0); // BDPT weight
			else {
				// MIS with direct light sampling
				const uint instance_index = gPathVertex.instance_index();
				if ((gSampleEnvironment && instance_index == INVALID_INSTANCE) || (gSampleEmissive && instance_index != INVALID_INSTANCE)) {
					w = 0.5; // average
					if (gUseMIS) {
						float pdfA, G, dist;
						float3 to_light;
						light_sample_pdf(instance_index, gPathShadingData, gPathState.prev_vertex_position, to_light, dist, pdfA, G);
						if (pdfA > 0)
							w = mis_heuristic(pdfWtoA(gPathState.dir_out_pdfW, G) * transmit_dir_pdf, pdfA * transmit_nee_pdf);
					}
				}
			}
			gRadiance[index.xy].rgb += gPathVertex.beta * L * w;
		}

		// terminate path
		if (!has_bsdf) break;

		if (depth >= gPushConstants.gMinDepth) {
			const float l = min(max3(gPathVertex.beta) / gPathState.eta_scale, 0.95);
			if (rng_next_float(index_1d) > l)
				break;
			else
				gPathVertex.beta /= l;
		}
	}
	if (gTraceLightPaths)
		gLightPathVertices[w*h*depth + index_1d].beta = 0;
}

[numthreads(8, 8, 1)]
void resolve(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gRadiance.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float4 radiance = gRadiance[index.xy];
	if (gSamplingFlags & SAMPLE_FLAG_DEMODULATE_ALBEDO) {
		const float3 albedo = gAlbedo[index.xy].rgb;
		if (albedo.r > 0) radiance.r /= albedo.r;
		if (albedo.g > 0) radiance.g /= albedo.g;
		if (albedo.b > 0) radiance.b /= albedo.b;
	}
	gRadiance[index.xy] = radiance;
}