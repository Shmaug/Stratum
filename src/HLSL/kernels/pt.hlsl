#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E store_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E store_albedo_and_reservoirs
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_path_bounce

[[vk::constant_id(0)]] const uint gMaterialType = 0;

#include "../scene.hlsli"
#include "../tonemap.hlsli"
#include "../reservoir.hlsli"

#define gVolumeCount 8
#define gImageCount 1024

[[vk::binding(0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding(2,0)]] ByteAddressBuffer gIndices;
[[vk::binding(3,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding(4,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(5,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding(6,0)]] RWByteAddressBuffer gCounters;
[[vk::binding(7,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(8,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(9,0)]] SamplerState gSampler;
[[vk::binding(10,0)]] Texture2D<float4> gImages[gImageCount];

#include "../path_state.hlsli"

[[vk::binding(0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding(2,1)]] StructuredBuffer<uint> gViewVolumeInstances;
[[vk::binding(3,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding(4,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(5,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(6,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(7,1)]] RWStructuredBuffer<PathVertexGeometry> gPathVertices;
[[vk::binding(8,1)]] RWStructuredBuffer<LightSampleRecord> gLightSamples;
#define DECLARE_VISIBILITY_BUFFERS \
	[[vk::binding(9,1)]] RWTexture2D<uint4> gVisibility[VISIBILITY_BUFFER_COUNT]; \
	[[vk::binding(9+VISIBILITY_BUFFER_COUNT,1)]] RWTexture2D<uint4> gPrevVisibility[VISIBILITY_BUFFER_COUNT];
#include "../visibility_buffer.hlsli"

[[vk::push_constant]] const PathTracePushConstants gPushConstants;
static const bool gSampleBG     = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_BG_IS)    && gPushConstants.gEnvironmentSampleProbability > 0 && gPushConstants.gEnvironmentMaterialAddress != -1;
static const bool gSampleLights = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

#include "../image_value.hlsli"

inline float4 sample_image(Texture2D<float4> img, const float2 uv, const float uv_screen_size) {
	float w,h;
	img.GetDimensions(w,h);
	return img.SampleLevel(gSampler, uv, (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RAY_CONE_LOD) ? log2(max(uv_screen_size*min(w,h), 1e-8f)) : 0);
}
inline float  sample_image(const uint vertex, const ImageValue1 img) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), gPathVertices[vertex].uv, gPathVertices[vertex].uv_screen_size)[img.channel()];
}
inline float2 sample_image(const uint vertex, const ImageValue2 img) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), gPathVertices[vertex].uv, gPathVertices[vertex].uv_screen_size).rg;
}
inline float3 sample_image(const uint vertex, const ImageValue3 img) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), gPathVertices[vertex].uv, gPathVertices[vertex].uv_screen_size).rgb;
}
inline float4 sample_image(const uint vertex, const ImageValue4 img) {
	if (!img.has_image()) return img.value;
	return img.value * sample_image(img.image(), gPathVertices[vertex].uv, gPathVertices[vertex].uv_screen_size);
}

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
	gPathStates[index_1d].rng_state.w++;
	return pcg4d(gPathStates[index_1d].rng_state).x;
}
inline float rng_next_float(const uint index_1d) { return asfloat(0x3f800000 | (rng_next_uint(index_1d) >> 9)) - 1; }

#define PNANOVDB_HLSL
#include "../../extern/nanovdb/PNanoVDB.h"

#include "../material.hlsli"
#include "../light.hlsli"

inline bool is_volume(const uint instance_index) {
	const uint material_address = instance_index == INVALID_INSTANCE ? gPushConstants.gEnvironmentMaterialAddress : gInstances[instance_index].material_address();
	if (material_address == -1) return false;
	const uint type = gMaterialData.Load(material_address);
	return type == BSDFType::eHeterogeneousVolume;
}
inline uint path_material_address(const uint index_1d) {
	const uint instance_index = gPathStates[index_1d].instance_index();
	if (instance_index == INVALID_INSTANCE)
		return gPushConstants.gEnvironmentMaterialAddress;
	else
		return gInstances[instance_index].material_address();
}
inline RayDifferential path_ray(const uint index_1d) {
	RayDifferential r;
	r.origin = gPathStates[index_1d].ray_origin;
	r.direction = normalize(gPathVertices[index_1d].position - r.origin);
	r.t_min = 0;
	r.t_max = 1.#INF;
	r.radius = gPathStates[index_1d].radius();
	r.spread = gPathStates[index_1d].spread();
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
		intersect(rayQuery, tmp_ray, gPathVertices[index_1d], gPathStates[index_1d].instance_primitive_index);
		
		if (is_volume(gPathStates[index_1d].vol_index)) {
			// interact with volume
			const HeterogeneousVolume::DeltaTrackResult tr = load_material<HeterogeneousVolume>(gMaterialData, gInstances[gPathStates[index_1d].vol_index].material_address() + 4).delta_track(
				gInstances[gPathStates[index_1d].vol_index].inv_transform.transform_point(tmp_ray.origin),
				gInstances[gPathStates[index_1d].vol_index].inv_transform.transform_vector(tmp_ray.direction),
				rayQuery.CommittedRayT(), index_1d);
			gPathStates[index_1d].throughput *= tr.transmittance / average(tr.dir_pdf);
			transmit_dir_pdf *= average(tr.dir_pdf);
			transmit_nee_pdf *= average(tr.nee_pdf);
			if (all(isfinite(tr.scatter_p))) {
				uint instance_primitive_index = 0;
				BF_SET(instance_primitive_index, gPathStates[index_1d].vol_index, 0, 16);
				BF_SET(instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				gPathStates[index_1d].instance_primitive_index = instance_primitive_index;
				instance_volume_geometry(gPathVertices[index_1d], gPathStates[index_1d].vol_index, tr.scatter_p);
				gPathVertices[index_1d].uv_screen_size = ray.differential_transfer(length(ray.origin - gPathVertices[index_1d].position)) / gPathVertices[index_1d].inv_uv_size;
				return;
			}
		}

		if (gPathStates[index_1d].instance_index() != INVALID_INSTANCE && is_volume(gPathStates[index_1d].instance_index())) {
			gPathStates[index_1d].vol_index = gPathVertices[index_1d].front_face ? gPathStates[index_1d].instance_index() : INVALID_INSTANCE;
			tmp_ray.origin = ray_offset(gPathVertices[index_1d].position, gPathVertices[index_1d].front_face ? -gPathVertices[index_1d].geometry_normal : gPathVertices[index_1d].geometry_normal);
		} else
			return;
	}
}
inline float3 eval_transmittance(inout ray_query_t rayQuery, const uint index_1d, const float3 direction, const float t_max, out float dir_pdf, out float nee_pdf) {
	float3 transmittance = 1;
	dir_pdf = 1;
	nee_pdf = 1;

	const bool inside = dot(direction, gPathVertices[index_1d].geometry_normal) < 0;
	uint cur_vol_instance = (gPathVertices[index_1d].shape_area && inside) ? gPathStates[index_1d].instance_index() : gPathStates[index_1d].vol_index;

	RayDifferential ray;
	ray.origin = gPathVertices[index_1d].shape_area == 0 ? gPathVertices[index_1d].position : ray_offset(gPathVertices[index_1d].position, inside ? -gPathVertices[index_1d].geometry_normal : gPathVertices[index_1d].geometry_normal);
	ray.direction = direction;
	ray.t_min = 0;
	ray.t_max = t_max*0.999;
	while (ray.t_max > 1e-6f) {
		PathVertexGeometry v;
		uint instance_primitive_index;
		intersect(rayQuery, ray, v, instance_primitive_index);
		const float dt = (BF_GET(instance_primitive_index,0,16) == INVALID_INSTANCE) ? ray.t_max : length(v.position - ray.origin);

		if (is_volume(cur_vol_instance)) {
			// interact with volume
			const HeterogeneousVolume::DeltaTrackResult vt = load_material<HeterogeneousVolume>(gMaterialData, gInstances[cur_vol_instance].material_address() + 4).delta_track(
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
			ray.t_max = isinf(t_max) ? t_max : length(ray_offset(ray.origin+ray.direction*t_max, -ray.direction) - ray.origin);
			continue;
		} else {
			// hit a surface
			transmittance = 0;
			dir_pdf = 0;
			nee_pdf = 0;
			break;
		}
	}
	return transmittance;
}

inline float3 sample_light(inout ray_query_t rayQuery, const uint index_1d) {
	const float3 dir_in = -path_ray(index_1d).direction;
	LightSampleRecord ls;
	sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), index_1d, dir_in);
	if (ls.pdf <= 0) return 0;

	float T_dir_pdf, T_nee_pdf;
	const float3 T = eval_transmittance(rayQuery, index_1d, ls.to_light, ls.dist, T_dir_pdf, T_nee_pdf);
	if (all(T <= 0)) return 0;
	
	const BSDFEvalRecord f = load_and_eval_material(gMaterialData, path_material_address(index_1d), dir_in, ls.to_light, index_1d, TRANSPORT_TO_LIGHT);
	if (f.pdfW <= 0) return 0;

	float3 C1 = f.f * ls.G * ls.radiance / ls.pdfA;

	C1 *= T / T_nee_pdf;

	const float w = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) ?
		mis_heuristic(ls.pdfA*T_nee_pdf, pdfWtoA(f.pdfW, ls.G)*T_dir_pdf) : 0.5;
	return C1 * w;
}
inline float3 sample_reservoir(inout ray_query_t rayQuery, const uint index_1d) {
	uint2 extent;
	gRadiance.GetDimensions(extent.x,extent.y);
	const uint light_vertex = extent.x*extent.y + index_1d;
	instance_geometry(gPathVertices[light_vertex], gReservoirs[index_1d].light_sample.instance_primitive_index, gReservoirs[index_1d].light_sample.position_or_bary, gReservoirs[index_1d].light_sample.position_or_bary.xy);

	float3 to_light = gPathVertices[light_vertex].position - gPathVertices[index_1d].position;
	const float dist = length(to_light);
	const float rcp_dist = 1/dist;
	to_light *= rcp_dist;
	
	float T_dir_pdf, T_nee_pdf;
	const float3 T = eval_transmittance(rayQuery, index_1d, to_light, dist, T_dir_pdf, T_nee_pdf);
	if (all(T <= 0)) return 0;

	const BSDFEvalRecord f = load_and_eval_material(gMaterialData, path_material_address(index_1d), -path_ray(index_1d).direction, to_light, index_1d, TRANSPORT_TO_LIGHT);
	if (f.pdfW <= 0) return 0;
	
	const uint light_material_address = gReservoirs[index_1d].light_sample.instance_index() == INVALID_INSTANCE ?
		gPushConstants.gEnvironmentMaterialAddress : gInstances[gReservoirs[index_1d].light_sample.instance_index()].material_address();
	const float3 L = load_and_eval_material_emission(gMaterialData, light_material_address, light_vertex);
	float w;
	float pdf, pdfA, G;
	light_sample_pdf(gReservoirs[index_1d].light_sample.instance_index(), light_vertex, gPathVertices[index_1d].position, pdf, pdfA, G);
	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) {
		w = mis_heuristic(pdfA*T_nee_pdf, pdfWtoA(f.pdfW,G)*T_dir_pdf);
	} else
		w = 0.5;

	return T/T_nee_pdf * f.f * L * G * gReservoirs[index_1d].W() * w;
}
inline float3 sample_bounce(inout ray_query_t rayQuery, const uint index_1d) {
	const RayDifferential ray_in = path_ray(index_1d);
	const BSDFSampleRecord bsdf_sample = load_and_sample_material(gMaterialData, path_material_address(index_1d), float3(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), -ray_in.direction, index_1d, TRANSPORT_TO_LIGHT);
	if (bsdf_sample.eval.pdfW <= 0) {
		gPathStates[index_1d].throughput = 0;
		return 0;
	}
	gPathStates[index_1d].throughput *= bsdf_sample.eval.f / bsdf_sample.eval.pdfW;

	const bool inside = dot(bsdf_sample.dir_out, gPathVertices[index_1d].geometry_normal) < 0;

	RayDifferential ray_out;
	ray_out.origin = gPathVertices[index_1d].shape_area == 0 ? gPathVertices[index_1d].position : ray_offset(gPathVertices[index_1d].position, inside ? -gPathVertices[index_1d].geometry_normal : gPathVertices[index_1d].geometry_normal);
	ray_out.t_min = 0;
	ray_out.direction = bsdf_sample.dir_out;
	ray_out.t_max = 1.#INF;
	ray_out.radius = gPathStates[index_1d].instance_index() == INVALID_INSTANCE ? ray_in.radius : ray_in.differential_transfer(length(gPathVertices[index_1d].position - ray_in.origin));
	if (bsdf_sample.eta == 0) {
		ray_out.spread = ray_in.differential_reflect(gPathVertices[index_1d].mean_curvature, bsdf_sample.roughness);
	} else if (bsdf_sample.eta > 0) {
		ray_out.spread = ray_in.differential_refract(gPathVertices[index_1d].mean_curvature, bsdf_sample.roughness, bsdf_sample.eta);
		gPathStates[index_1d].eta_scale /= bsdf_sample.eta*bsdf_sample.eta;
	} else
		ray_out.spread = ray_in.spread;
	
	gPathStates[index_1d].ray_origin = ray_out.origin;
	gPathStates[index_1d].radius_spread = pack_f16_2(float2(ray_out.radius, ray_out.spread));
	
	float transmit_dir_pdf, transmit_nee_pdf;
	intersect_and_scatter(rayQuery, ray_out, index_1d, transmit_dir_pdf, transmit_nee_pdf);

	const uint material_address = path_material_address(index_1d);
	if (material_address == INVALID_MATERIAL)
		return 0;

	const float3 L = load_and_eval_material_emission(gMaterialData, material_address, index_1d);
	if (any(L > 0)) {
		float w = 1;
		if ((gSampleLights || gSampleBG) && bsdf_sample.eval.pdfW > 0) {
			w = 0.5;
			if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) {
				float pdf, pdfA, G;
				light_sample_pdf(gPathStates[index_1d].instance_index(), index_1d, gPathStates[index_1d].ray_origin, pdf, pdfA, G);
				if (pdf > 0)
					w = mis_heuristic(pdfWtoA(bsdf_sample.eval.pdfW, G) * transmit_dir_pdf, pdfA * transmit_nee_pdf);
			}
		}
		return gPathStates[index_1d].throughput * L * w;
	}
	return 0;
}

#define GROUP_SIZE 8

template<typename Material>
inline void material_reservoir_ris(const Material material, const uint index_1d) {
	gReservoirs[index_1d] = init_reservoir();
	const float3 dir_in = -path_ray(index_1d).direction;
	for (uint i = 0; i < gPushConstants.gReservoirSamples; i++) {
		LightSampleRecord ls;
		sample_light_or_environment(ls, float4(rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d), rng_next_float(index_1d)), index_1d, dir_in);
		if (ls.pdf <= 0) continue;
		const BSDFEvalRecord f = eval_material(material, dir_in, ls.to_light, index_1d, TRANSPORT_TO_LIGHT);
		if (f.pdfW <= 0) continue;
		const ReservoirLightSample s = { ls.position_or_bary, ls.instance_primitive_index };
		gReservoirs[index_1d].update(rng_next_float(index_1d), s, ls.pdfA, luminance(f.f * ls.radiance) * ls.G);
	}
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void trace_visibility(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	const float2 uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	const RayDifferential view_ray = gViews[view_index].create_ray(uv, gViews[view_index].extent());

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	gPathStates[index_1d].rng_state = uint4(index.xy, gPushConstants.gRandomSeed, 0);

	gPathStates[index_1d].ray_origin = view_ray.origin;
	gPathStates[index_1d].radius_spread = pack_f16_2(float2(view_ray.radius, view_ray.spread));
	gPathStates[index_1d].vol_index = gViewVolumeInstances[view_index];
	gPathStates[index_1d].throughput = 1;
	gPathStates[index_1d].eta_scale = 1;

	ray_query_t rayQuery;
	float dir_pdf, nee_pdf;
	intersect_and_scatter(rayQuery, view_ray, index_1d, dir_pdf, nee_pdf);

	gVisibility[0][index.xy] = gPathStates[index_1d].rng_state;
	gVisibility[1][index.xy] = uint4(gPathStates[index_1d].instance_primitive_index, asuint(rayQuery.CommittedTriangleBarycentrics()), pack_normal_octahedron(gPathVertices[index_1d].shading_normal));
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void store_visibility(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	float2 prev_uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float prev_z = 1.#INF;
	float2 dz = 0;

	if (gPathStates[index_1d].instance_index() != INVALID_INSTANCE) {
		switch (gInstances[gPathStates[index_1d].instance_index()].type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			// TODO: figure out dz
			//const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(gPathVertices[index_1d].g.geometry_normal);
			//dz = 1/(abs(view_normal.xy) + 1e-2);
			dz = 1;
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			dz = 1/sqrt(gInstances[gPathStates[index_1d].instance_index()].radius());
			break;
		case INSTANCE_TYPE_VOLUME:
			dz = 1;
			break;
		}
		
		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, gInstances[gPathStates[index_1d].instance_index()].prev_transform).transform_point(gPathVertices[index_1d].position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	}

	const float z = gPathStates[index_1d].instance_index() == INVALID_INSTANCE ? 1.#INF : length(gPathStates[index_1d].ray_origin - gPathVertices[index_1d].position);	
	gVisibility[2][index.xy] = uint4(pack_f16_2(float2(z, prev_z)), pack_f16_2(dz), asuint(prev_uv));
	
	switch (gPushConstants.gDebugMode) {
		default:
			break;
		case DebugMode::eZ:
			gRadiance[index.xy].rgb = viridis_quintic(1 - exp(-0.1*z));
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eDz:
			gRadiance[index.xy].rgb = viridis_quintic(length(dz));
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eShadingNormal:
			gRadiance[index.xy].rgb = gPathVertices[index_1d].shading_normal*.5 + .5;
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eGeometryNormal:
			gRadiance[index.xy].rgb = gPathVertices[index_1d].geometry_normal*.5 + .5;
			gPathStates[index_1d].throughput = 0;
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
			gPathStates[index_1d].throughput = 0;
			break;
		}
		case DebugMode::eTangent:
			gRadiance[index.xy].rgb = gPathVertices[index_1d].tangent.xyz*.5 + .5;
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eMeanCurvature:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(gPathVertices[index_1d].mean_curvature));
			gPathStates[index_1d].throughput = 0;
			break;
		case eRayRadius:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(100*gPathStates[index_1d].radius()));
			gPathStates[index_1d].throughput = 0;
			break;
		case eUVScreenSize:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(log2(1024*gPathVertices[index_1d].uv_screen_size)/10));
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::ePrevUV:
			gRadiance[index.xy].rgb = float3(prev_uv, 0);
			gPathStates[index_1d].throughput = 0;
			break;
	}
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void store_albedo_and_reservoirs(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	// primary albedo and emission
	const uint material_address = path_material_address(index_1d);
	if (material_address == INVALID_MATERIAL) {
		gRadiance[index.xy] = 0;
		gAlbedo[index.xy] = 0;
		gPathStates[index_1d].throughput = 0;
	} else {
		const uint type = gMaterialData.Load(material_address);
		if (type != gMaterialType) return;
		switch (gMaterialType) {
		#define CASE_FN(Material) \
		case e##Material: {\
			const Material material = load_material<Material>(gMaterialData, material_address+4); \
			gRadiance[index.xy] = float4(gPathStates[index_1d].throughput * eval_material_emission(material, index_1d), 1); \
			gAlbedo  [index.xy] = float4(gPathStates[index_1d].throughput *   eval_material_albedo(material, index_1d), 1); \
			if ((gSampleLights || gSampleBG) && gPushConstants.gReservoirSamples > 0 && gPathStates[index_1d].instance_index() != INVALID_INSTANCE) \
				material_reservoir_ris(material, index_1d); \
			break; \
		}
		FOR_EACH_BSDF_TYPE(CASE_FN);
		#undef CASE_FN
		}

		if (gPathStates[index_1d].instance_index() == INVALID_INSTANCE)
			gPathStates[index_1d].throughput = 0;
	}
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void sample_path_bounce(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;
	
	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;
	if (all(gPathStates[index_1d].throughput <= 1e-6)) return;

	ray_query_t rayQuery;
	if (gSampleLights || gSampleBG) {
		if (gPushConstants.gReservoirSamples > 0)
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_reservoir(rayQuery, index_1d);
		else
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_light(rayQuery, index_1d);
	}
	gRadiance[index.xy].rgb += sample_bounce(rayQuery, index_1d);

	if (gPathStates[index_1d].instance_index() == INVALID_INSTANCE) {
		gPathStates[index_1d].throughput = 0;
		return;
	}

	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RR) {
		const float l = min(max3(gPathStates[index_1d].throughput) / gPathStates[index_1d].eta_scale, 0.95);
		if (rng_next_float(index_1d) > l) {
			gPathStates[index_1d].throughput = 0;
			return;
		} else
			gPathStates[index_1d].throughput /= l;
	}
}