#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_path_bounce

#include "../scene.hlsli"
#include "../tonemap.hlsli"
#include "../reservoir.hlsli"

#define gVolumeCount 8
#define gImageCount 1024

RaytracingAccelerationStructure gScene;
StructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gIndices;
ByteAddressBuffer gMaterialData;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<uint> gLightInstances;

StructuredBuffer<ViewData> gViews;
StructuredBuffer<ViewData> gPrevViews;
StructuredBuffer<uint> gViewVolumeIndices;

#include "../visibility_buffer.hlsli"
RWStructuredBuffer<Reservoir> gReservoirs;
RWStructuredBuffer<PathBounceState> gPathStates;

RWTexture2D<float4> gRadiance;
RWTexture2D<float4> gAlbedo;

RWByteAddressBuffer gCounters;

StructuredBuffer<float> gDistributions;
StructuredBuffer<uint> gVolumes[gVolumeCount];
SamplerState gSampler;
Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] const struct {
	uint gRandomSeed;
	uint gLightCount;
	uint gViewCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;	
	uint gReservoirSamples;
	uint gSamplingFlags;
	uint gMaxNullCollisions;
	uint gDebugMode;
} gPushConstants;

static const bool gSampleBG     = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_BG_IS)    && gPushConstants.gEnvironmentSampleProbability > 0 && gPushConstants.gEnvironmentMaterialAddress != -1;
static const bool gSampleLights = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

struct rng_t {
	uint4 state;

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

	inline uint nexti() {
		state.w++;
		return pcg4d(state).x;
	}
	inline float next() { return asfloat(0x3f800000 | (nexti() >> 9)) - 1; }
};

#define PNANOVDB_HLSL
#include "../../extern/nanovdb/PNanoVDB.h"

#include "../path_vertex.hlsli"

#include "../material.hlsli"
#include "../light.hlsli"

inline bool is_volume(const uint instance_index) {
	const uint material_address = instance_index == INVALID_INSTANCE ? gPushConstants.gEnvironmentMaterialAddress : gInstances[instance_index].material_address();
	if (material_address == -1) return false;
	const uint type = gMaterialData.Load(material_address);
	return type == BSDFType::eHeterogeneousVolume;
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
						pnanovdb_grid_handle_t grid = pnanovdb_grid_handle_t(0);
						pnanovdb_root_handle_t root = pnanovdb_tree_get_root(buf, pnanovdb_grid_get_tree(buf, grid));
						const float3 origin    = pnanovdb_grid_world_to_indexf(buf, grid, rayQuery.CandidateObjectRayOrigin());
						const float3 direction = pnanovdb_grid_world_to_index_dirf(buf, grid, rayQuery.CandidateObjectRayDirection());
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
							vol_normal = normalize(instance.transform.transform_vector(pnanovdb_grid_index_to_world_dirf(buf, grid, vol_normal)));
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
inline void intersect(inout ray_query_t rayQuery, const RayDifferential ray, inout PathVertex v) {
	float3 vol_normal;
	if (do_ray_query(rayQuery, ray, vol_normal)) {
		// hit an instance
		BF_SET(v.instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		const InstanceData instance = gInstances[v.instance_index()];
		v.material_address = instance.material_address();
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
			const float3 local_pos = rayQuery.CommittedObjectRayOrigin() + rayQuery.CommittedObjectRayDirection()*rayQuery.CommittedRayT();
			switch (instance.type()) {
				case INSTANCE_TYPE_SPHERE:
					v.g = instance_sphere_geometry(instance, local_pos);
					break;
				case INSTANCE_TYPE_VOLUME:
					v.g = instance_volume_geometry(instance, local_pos);
					v.g.geometry_normal = v.g.shading_normal = vol_normal;
					break;
			}
		} else {
			// triangle
			BF_SET(v.instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			v.g = instance_triangle_geometry(instance, load_tri(gIndices, instance, v.primitive_index()), rayQuery.CommittedTriangleBarycentrics());
		}
		v.g.front_face = dot(ray.direction, v.g.geometry_normal) < 0;
		v.ray_radius = ray.differential_transfer(rayQuery.CommittedRayT());
	} else {
		// background
		BF_SET(v.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		v.material_address = gPushConstants.gEnvironmentMaterialAddress;
		v.g.position = ray.direction;
		v.g.geometry_normal = v.g.shading_normal = ray.direction;
		v.g.tangent.w = 1;
		v.g.shape_area = 0;
		v.g.uv = cartesian_to_spherical_uv(ray.direction);
		v.g.mean_curvature = 1;
		v.g.inv_uv_size = 1;
		v.ray_radius = ray.radius;
	}
	v.g.uv_screen_size = v.ray_radius / v.g.inv_uv_size;
}
inline void intersect_and_scatter(inout ray_query_t rayQuery, const RayDifferential ray, inout PathVertex v, inout uint cur_vol, inout rng_t rng, inout float3 throughput, out float transmit_pdf) {
	transmit_pdf = 1;
	RayDifferential tmp_ray = ray;
	for (uint steps = 0; steps < 64; steps++) {
		intersect(rayQuery, tmp_ray, v);
		
		if (is_volume(cur_vol)) {
			// interact with volume
			HeterogeneousVolume vol;
			uint tmp = gInstances[cur_vol].material_address() + 4;
			vol.load(gMaterialData, tmp);
			const HeterogeneousVolume::DeltaTrackResult tr = vol.delta_track(
				gInstances[cur_vol].inv_transform.transform_point(tmp_ray.origin),
				gInstances[cur_vol].inv_transform.transform_vector(tmp_ray.direction),
				rayQuery.CommittedRayT(), rng);
			throughput *= tr.transmittance / average(tr.pdf);
			if (all(isfinite(tr.scatter_p))) {
				BF_SET(v.instance_primitive_index, cur_vol, 0, 16);
				BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
				v.material_address = gInstances[cur_vol].material_address();
				v.g = instance_volume_geometry(gInstances[cur_vol], tr.scatter_p);
				v.ray_radius = ray.differential_transfer(length(ray.origin - v.g.position));
				return;
			} else
				transmit_pdf *= average(tr.pdf);
		}

		if (v.instance_index() != INVALID_INSTANCE && is_volume(v.instance_index())) {
			cur_vol = v.g.front_face ? v.instance_index() : INVALID_INSTANCE;
			tmp_ray.origin = ray_offset(v.g.position, v.g.front_face ? -v.g.geometry_normal : v.g.geometry_normal);
		} else
			return;
	}
}
inline float3 transmittance_along_ray(inout ray_query_t rayQuery, inout rng_t rng, const float3 origin, const float3 direction, const float t_max, uint cur_vol, out float pdf) {
	float3 transmittance = 1;
	pdf = 1;

	RayDifferential ray;
	ray.origin = origin;
	ray.direction = direction;
	ray.t_min = 0;
	ray.t_max = isinf(t_max) ? t_max : length(ray_offset(origin+direction*t_max, -direction) - ray.origin);
	while (ray.t_max > 1e-6f) {
		PathVertex v;
		intersect(rayQuery, ray, v);
		const float dt = (v.instance_index() == INVALID_INSTANCE) ? ray.t_max : length(ray.origin - v.g.position);

		if (is_volume(cur_vol)) {
			// interact with volume
			HeterogeneousVolume vol;
			uint tmp = gInstances[cur_vol].material_address() + 4;
			vol.load(gMaterialData, tmp);
			const HeterogeneousVolume::DeltaTrackResult vt = vol.delta_track(
				gInstances[cur_vol].inv_transform.transform_point(ray.origin),
				gInstances[cur_vol].inv_transform.transform_vector(ray.direction),
				dt, rng, false);
			transmittance *= vt.transmittance;
			pdf *= average(vt.pdf);
		}

		if (v.instance_index() == INVALID_INSTANCE) break;
		
		if (is_volume(v.instance_index())) {
			// hit a volume
			cur_vol = v.g.front_face ? v.instance_index() : INVALID_INSTANCE;
			ray.origin = ray_offset(v.g.position, v.g.front_face ? -v.g.geometry_normal : v.g.geometry_normal);
			ray.t_max = isinf(t_max) ? t_max : length(ray_offset(origin+direction*t_max, -direction) - ray.origin);
			continue;
		} else {
			// hit a surface
			transmittance = 0;
			pdf = 0;
			break;
		}
	}
	return transmittance;
}

inline float3 sample_direct_light(const PathVertex vertex, const float3 dir_in, inout ray_query_t rayQuery, inout rng_t rng, const uint cur_vol) {
	const LightSampleRecord light_sample = sample_light_or_environment(float4(rng.next(), rng.next(), rng.next(), rng.next()), vertex.g, dir_in);
	if (light_sample.pdf.pdf <= 0) return 0;

	const BSDFEvalRecord f = eval_material<true>(gMaterialData, vertex.material_address, dir_in, light_sample.to_light, vertex.g);
	if (f.pdfW <= 0) return 0;

	float3 C1 = f.f * light_sample.radiance / light_sample.pdf.pdf;
	if (!light_sample.pdf.is_solid_angle)
		C1 *= light_sample.pdf.G;
	
	const bool inside = dot(light_sample.to_light, vertex.g.geometry_normal) < 0;
	const float3 origin = vertex.g.shape_area == 0 ? vertex.g.position : ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	float T_pdf;
	const float3 T = transmittance_along_ray(rayQuery, rng, origin, light_sample.to_light, light_sample.dist, (vertex.g.shape_area && inside) ? vertex.instance_index() : cur_vol, T_pdf);
	if (all(T <= 0)) return 0;

	C1 *= T / T_pdf;

	const float w = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) ?
		mis_heuristic(light_sample.pdf.solid_angle(), f.pdfW*T_pdf) : 0.5;
	return C1 * w;
}
inline float3 sample_reservoir(const PathVertex vertex, const float3 dir_in, inout ray_query_t rayQuery, inout rng_t rng, const uint cur_vol, const Reservoir r) {
	const PathVertexGeometry light_vertex = instance_geometry(r.light_sample.instance_primitive_index, r.light_sample.position_or_bary, r.light_sample.position_or_bary.xy);

	float3 to_light = light_vertex.position - vertex.g.position;
	const float dist = length(to_light);
	const float rcp_dist = 1/dist;
	to_light *= rcp_dist;
	
	const BSDFEvalRecord f = eval_material<true>(gMaterialData, vertex.material_address, dir_in, to_light, vertex.g);
	if (f.pdfW <= 0) return 0;
	
	const bool inside = dot(to_light, vertex.g.geometry_normal) < 0;

	const float3 origin = vertex.g.shape_area == 0 ? vertex.g.position : ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	float T_pdf;
	const float3 T = transmittance_along_ray(rayQuery, rng, origin, to_light, dist, (vertex.g.shape_area && inside) ? vertex.instance_index() : cur_vol, T_pdf);
	if (all(T <= 0)) return 0;

	const uint light_material_address = r.light_sample.instance_index() == INVALID_INSTANCE ?
		gPushConstants.gEnvironmentMaterialAddress : gInstances[r.light_sample.instance_index()].material_address();
	const float3 L = eval_material_emission(gMaterialData, light_material_address, light_vertex);
	const float G = abs(dot(light_vertex.geometry_normal, to_light)) * (rcp_dist*rcp_dist);
	float w;
	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) {
		PathVertex lv;
		lv.instance_primitive_index = r.light_sample.instance_primitive_index;
		lv.material_address = light_material_address;
		lv.g = light_vertex;
		const PDFMeasure pdf = light_sample_pdf(lv, vertex.g.position);
		w = mis_heuristic(pdf.solid_angle(), f.pdfW*T_pdf);
	} else
		w = 0.5;

	return T/T_pdf * f.f * L * G * r.W() * w;
}
inline float3 sample_indirect_light(inout PathVertex vertex, inout RayDifferential ray, inout ray_query_t rayQuery, inout rng_t rng, const uint index_1d) {
	const float3 dir_in = -ray.direction;

	const BSDFSampleRecord bsdf_sample = sample_material<true>(gMaterialData, vertex.material_address, float3(rng.next(), rng.next(), rng.next()), dir_in, vertex.g);
	if (bsdf_sample.eval.pdfW <= 0) {
		gPathStates[index_1d].throughput = 0;
		return 0;
	}
	gPathStates[index_1d].throughput *= bsdf_sample.eval.f / bsdf_sample.eval.pdfW;

	ray.direction = bsdf_sample.dir_out;
	if (bsdf_sample.eta == 0) {
		ray.differential_reflect(vertex.g.mean_curvature, bsdf_sample.roughness);
	} else if (bsdf_sample.eta > 0) {
		ray.differential_refract(vertex.g.mean_curvature, bsdf_sample.roughness, bsdf_sample.eta);
		gPathStates[index_1d].eta_scale /= bsdf_sample.eta*bsdf_sample.eta;
	}
	
	const bool inside = dot(bsdf_sample.dir_out, vertex.g.geometry_normal) < 0;
	ray.origin = vertex.g.shape_area == 0 ? vertex.g.position : ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	
	float transmit_pdf;
	intersect_and_scatter(rayQuery, ray, vertex, gPathStates[index_1d].vol_stack[0], rng, gPathStates[index_1d].throughput, transmit_pdf);

	gPathStates[index_1d].position = vertex.g.position;
	gPathStates[index_1d].ray_origin = ray.origin;
	gPathStates[index_1d].radius_spread = pack_f16_2(float2(vertex.ray_radius, ray.spread));
	gPathStates[index_1d].instance_primitive_index = vertex.instance_primitive_index;
	
	if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		gPathStates[index_1d].bary = rayQuery.CommittedTriangleBarycentrics();
	
	const float3 L = eval_material_emission(gMaterialData, vertex.material_address, vertex.g);
	if (any(L > 0)) {
		float w = 1;
		if ((gSampleLights || gSampleBG) && bsdf_sample.eval.pdfW > 0) {
			w = 0.5;
			if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) {
				const PDFMeasure pdf = light_sample_pdf(vertex, ray.origin);
				if (pdf.pdf > 0)
					w = mis_heuristic(bsdf_sample.eval.pdfW * transmit_pdf, pdf.solid_angle());
			}
		}
		return gPathStates[index_1d].throughput * L * w;
	}
	
	return 0;
}

#define GROUP_SIZE 8

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void trace_visibility(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	rng_t rng = { index.xy, gPushConstants.gRandomSeed, index.x + index.y };

	const float2 view_size = float2(gViews[view_index].image_max - gViews[view_index].image_min);
	const float2 uv = (index.xy + 0.5 - gViews[view_index].image_min)/view_size;
	RayDifferential view_ray = gViews[view_index].create_ray(uv, view_size);

	gPathStates[index_1d].eta_scale = 1;
	gPathStates[index_1d].ray_origin = view_ray.origin;

	ray_query_t rayQuery;
  PathVertex primary_vertex;
	gPathStates[index_1d].vol_stack[0] = gViewVolumeIndices[view_index];
	gPathStates[index_1d].vol_stack[1] = INVALID_INSTANCE;
	gPathStates[index_1d].throughput = 1;
	float pdf;
	intersect_and_scatter(rayQuery, view_ray, primary_vertex, gPathStates[index_1d].vol_stack[0], rng, gPathStates[index_1d].throughput, pdf);

	gRadiance[index.xy] = float4(gPathStates[index_1d].throughput * eval_material_emission(gMaterialData, primary_vertex.material_address, primary_vertex.g), 1);
	gAlbedo  [index.xy] = float4(gPathStates[index_1d].throughput *   eval_material_albedo(gMaterialData, primary_vertex.material_address, primary_vertex.g), 1);

	const float z = primary_vertex.instance_index() == INVALID_INSTANCE ? view_ray.t_max : length(view_ray.origin - primary_vertex.g.position);	
	float prev_z = 1.#INF;
	float2 dz = 0;
	float2 prev_uv = uv;

	if (primary_vertex.instance_index() != INVALID_INSTANCE) {
		const InstanceData instance = gInstances[primary_vertex.instance_index()];

		switch (instance.type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			gPathStates[index_1d].bary = rayQuery.CommittedTriangleBarycentrics();
			// TODO: figure out dz
			//const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(primary_vertex.g.geometry_normal);
			//dz = 1/(abs(view_normal.xy) + 1e-2);
			dz = 1;
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			gPathStates[index_1d].bary = primary_vertex.g.uv;
			dz = 1/sqrt(instance.radius());
			break;
		case INSTANCE_TYPE_VOLUME:
			gPathStates[index_1d].bary = 0;
			dz = 1;
			break;
		}

		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, instance.prev_transform).transform_point(primary_vertex.g.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	} else {
		gPathStates[index_1d].throughput = 0;
		const float3 prevCamPos = gPrevViews[view_index].world_to_camera.transform_point(view_ray.origin + view_ray.direction*100);
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	}

	store_visibility(index.xy, rng.state,
									 primary_vertex.instance_index(), primary_vertex.primitive_index(),
									 gPathStates[index_1d].bary, primary_vertex.g.shading_normal,
									 z, prev_z, dz, prev_uv);

	gPathStates[index_1d].radius_spread = pack_f16_2(float2(primary_vertex.ray_radius, view_ray.spread));
	gPathStates[index_1d].position = primary_vertex.g.position;
	gPathStates[index_1d].instance_primitive_index = primary_vertex.instance_primitive_index;

	if (gPushConstants.gReservoirSamples > 0 && (gSampleLights || gSampleBG) && primary_vertex.instance_index() != INVALID_INSTANCE) {
		Reservoir r = init_reservoir();
		for (uint i = 0; i < gPushConstants.gReservoirSamples; i++) {
			const LightSampleRecord light_sample = sample_light_or_environment(float4(rng.next(), rng.next(), rng.next(), rng.next()), primary_vertex.g, -view_ray.direction);
			if (light_sample.pdf.pdf <= 0) continue;
			const BSDFEvalRecord f = eval_material<true>(gMaterialData, primary_vertex.material_address, -view_ray.direction, light_sample.to_light, primary_vertex.g);
			if (f.pdfW <= 0) continue;
			const ReservoirLightSample s = { light_sample.position_or_bary, light_sample.instance_primitive_index };
			r.update(rng.next(), s, light_sample.pdf.pdf * light_sample.pdf.G, luminance(f.f * light_sample.radiance) * light_sample.pdf.G);
		}
		gReservoirs[index_1d] = r;
	}

	gPathStates[index_1d].rng_state = rng.state;

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
			gRadiance[index.xy].rgb = primary_vertex.g.shading_normal*.5 + .5;
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eGeometryNormal:
			gRadiance[index.xy].rgb = primary_vertex.g.geometry_normal*.5 + .5;
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eTangent:
			gRadiance[index.xy].rgb = primary_vertex.g.tangent.xyz*.5 + .5;
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::eMeanCurvature:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(primary_vertex.g.mean_curvature));
			gPathStates[index_1d].throughput = 0;
			break;
		case eRayRadius:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(100*primary_vertex.ray_radius));
			gPathStates[index_1d].throughput = 0;
			break;
		case eUVScreenSize:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(log2(1024*primary_vertex.g.uv_screen_size)/10));
			gPathStates[index_1d].throughput = 0;
			break;
		case DebugMode::ePrevUV:
			gRadiance[index.xy].rgb = float3(prev_uv, 0);
			gPathStates[index_1d].throughput = 0;
			break;
	}
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void trace_path_bounce(uint3 index : SV_DispatchThreadID) {
	const uint view_index = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (view_index == -1) return;
	
	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	if (all(gPathStates[index_1d].throughput <= 1e-6)) return;

	// load the current vertex and ray
	RayDifferential ray_in = gPathStates[index_1d].ray();
	PathVertex vertex;
	make_vertex(vertex, gPathStates[index_1d].instance_primitive_index, gPathStates[index_1d].position, gPathStates[index_1d].bary, ray_in);
	rng_t rng = { gPathStates[index_1d].rng_state };

	ray_query_t rayQuery;

	if (gSampleLights || gSampleBG) {
		if (gPushConstants.gReservoirSamples > 0)
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_reservoir(vertex, -ray_in.direction, rayQuery, rng, gPathStates[index_1d].vol_stack[0], gReservoirs[index_1d]);
		else
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_direct_light(vertex, -ray_in.direction, rayQuery, rng, gPathStates[index_1d].vol_stack[0]);
	}
	gRadiance[index.xy].rgb += sample_indirect_light(vertex, ray_in, rayQuery, rng, index_1d);

	if (vertex.instance_index() == INVALID_INSTANCE) {
		gPathStates[index_1d].throughput = 0;
		return;
	}

	if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		gPathStates[index_1d].bary = rayQuery.CommittedTriangleBarycentrics();

	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RR) {
		const float l = min(max3(gPathStates[index_1d].throughput) / gPathStates[index_1d].eta_scale, 0.95);
		if (rng.next() > l) {
			gPathStates[index_1d].throughput = 0;
			return;
		} else
			gPathStates[index_1d].throughput /= l;
	}
	
	gPathStates[index_1d].rng_state = rng.state;
}