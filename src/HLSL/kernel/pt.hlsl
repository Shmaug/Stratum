#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E trace_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E trace_indirect

#include "../scene.hlsli"

[[vk::constant_id(0)]] const uint gViewCount = 1;
[[vk::constant_id(1)]] uint gSampleCount = 1;
[[vk::constant_id(2)]] uint gMaxDepth = 3;

#define gImageCount 1024

RaytracingAccelerationStructure gScene;
StructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gIndices;
ByteAddressBuffer gMaterialData;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<uint> gLightInstances;

StructuredBuffer<ViewData> gViews;
StructuredBuffer<ViewData> gPrevViews;

#include "../visibility_buffer.hlsli"

RWTexture2D<float4> gRadiance;
RWTexture2D<float4> gAlbedo;

StructuredBuffer<float> gDistributions;
SamplerState gSampler;
Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] const struct {
	uint gMinDepth;
	uint gDirectLightDepth;
	uint gSamplingFlags;
	
	uint gRandomSeed;
	uint gLightCount;
	uint gViewCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;
	uint gHistoryValid;
} gPushConstants;

static const bool gSampleBG = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_BG_IS) && gPushConstants.gEnvironmentSampleProbability > 0;
static const bool gSampleLights = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

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
struct rng_t {
	uint4 v;
	
	inline uint nexti() {
		v.w++;
		return pcg4d(v).x;
	}
	inline float next() {
		v.w++;
		return asfloat(0x3f800000 | (nexti() >> 9)) - 1;
	}
};

struct PathVertexGeometry {
	float3 position;
	min16float shape_area;
  min16float3 geometry_normal;
  min16float3 shading_normal;
  min16float4 tangent;
	min16float2 uv;
	differential3 d_position;
	differential2 d_uv;

	inline ShadingFrame shading_frame() {
		ShadingFrame frame;
		frame.n = shading_normal;
		frame.t = tangent.xyz;
    frame.b = normalize(cross(frame.t, frame.n))*tangent.w;
		return frame;
	}
};
inline PathVertexGeometry make_sphere_geometry(const TransformData transform, const float radius, const RayDifferential ray, const float t) {
	PathVertexGeometry r;
	const float3 center = transform.transform_point(0);
	r.position = ray.origin + ray.direction*t;
	r.geometry_normal = r.shading_normal = (min16float3)normalize(r.position - center);
	r.tangent = min16float4((min16float3)cross(transform.transform_vector(float3(0, 1, 0)), r.geometry_normal), 1);
	r.uv = (min16float2)cartesian_to_spherical_uv(r.geometry_normal);
	// transfer ray differential
	r.d_position = transfer_dP(r.geometry_normal, ray.origin, ray.direction, t, ray.dP, ray.dD);
	r.d_uv.dx = 0;
	r.d_uv.dy = 0;
	r.shape_area = (min16float)(4*M_PI*radius*radius);
	return r;
}
inline PathVertexGeometry make_triangle_geometry(const TransformData transform, const RayDifferential ray, const uint3 tri, const float2 bary) {
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];

	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;

	PathVertexGeometry r;
	r.position       = v0.position + v1v0*bary.x + v2v0*bary.y;
	r.shading_normal = (min16float3)(v0.normal + (v1.normal - v0.normal)*bary.x + (v2.normal - v0.normal)*bary.y);
	r.tangent        = (min16float4)(v0.tangent + (v1.tangent - v0.tangent)*bary.x + (v2.tangent - v0.tangent)*bary.y);
	r.uv.x = (min16float)(v0.u + (v1.u - v0.u)*bary.x + (v2.u - v0.u)*bary.y);
	r.uv.y = (min16float)(v0.v + (v1.v - v0.v)*bary.x + (v2.v - v0.v)*bary.y);

	r.position = transform.transform_point(r.position);

	const float3 dPdu = transform.transform_vector(-v2v0);
	const float3 dPdv = transform.transform_vector(v1.position - v2.position);
	const float3 ng = cross(dPdu, dPdv);
	const float area2 = length(ng);
	r.geometry_normal = (min16float3)(ng/area2);
	r.shape_area = (min16float)(area2/2);

	if (all(r.shading_normal.xyz == 0) || any(isnan(r.shading_normal)))
		r.shading_normal = r.geometry_normal;
	else
		r.shading_normal = (min16float3)normalize(transform.transform_vector(r.shading_normal));
	
	if (r.tangent.w == 0 || all(r.tangent.xyz == 0) || any(isnan(r.tangent))) {
		float3 B;
		make_orthonormal(r.shading_normal, r.tangent.xyz, B);
		r.tangent.w = 1;
	} else {
		r.tangent.xyz = (min16float3)transform.transform_vector(r.tangent.xyz);
		r.tangent.xyz = normalize(r.tangent.xyz - dot(r.shading_normal, r.tangent.xyz)*r.shading_normal);
	}

	// transfer ray differential
	r.d_position = transfer_dP(r.geometry_normal, ray.origin, ray.direction, length(r.position - ray.origin), ray.dP, ray.dD);

	differential du, dv;
	differential_dudv(dPdu, dPdv, r.d_position, r.geometry_normal, du, dv);

	r.d_uv.dx = min16float2(du.dx*v0.u + dv.dx*v1.u - (du.dx + dv.dx)*v2.u, du.dx*v0.v + dv.dx*v1.v - (du.dx + dv.dx)*v2.v);
	r.d_uv.dy = min16float2(du.dy*v0.u + dv.dy*v1.u - (du.dy + dv.dy)*v2.u, du.dy*v0.v + dv.dy*v1.v - (du.dy + dv.dy)*v2.v);
	return r;
}

#include "../material.hlsli"

#define ray_query_t RayQuery<RAY_FLAG_FORCE_OPAQUE>
inline bool do_ray_query(inout ray_query_t rayQuery, const RayDifferential ray) {
	RayDesc rayDesc;
	rayDesc.Origin = ray.origin;
	rayDesc.Direction = ray.direction;
	rayDesc.TMin = ray.t_min;
	rayDesc.TMax = ray.t_max;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, rayDesc);
	while (rayQuery.Proceed()) {
		switch (rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE: {
				const float2 st = ray_sphere(rayQuery.CandidateObjectRayOrigin(), rayQuery.CandidateObjectRayDirection(), 0, 1);
				if (st.x < st.y) {
					const float t = st.x < 0 ? st.y : st.x;
					if (t <= rayQuery.CommittedRayT() && t >= rayQuery.RayTMin())
						rayQuery.CommitProceduralPrimitiveHit(t);
				}
				break;
			}
			case CANDIDATE_NON_OPAQUE_TRIANGLE: {
				//const InstanceData instance = gInstances[rayQuery.CandidateInstanceIndex()];
				//g = make_triangle_geometry(instance.transform, load_tri(gIndices, instance, rayQuery.CandidatePrimitiveIndex()), rayQuery.CommittedTriangleBarycentrics(), ray);
				rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}
	return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
}

struct PathVertex {
	uint instance_primitive_index;
	uint material_address;
	PathVertexGeometry g;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }

	inline BSDFSampleRecord sample_material(const float3 rnd, const float3 dir_in, const TransportDirection dir = TransportDirection::eToLight) {
		return ::sample_material(gMaterialData, material_address, rnd, dir_in, g, dir);
	}
	inline BSDFEvalRecord eval_material(const float3 dir_in, const float3 dir_out, const TransportDirection dir = TransportDirection::eToLight) {
		return ::eval_material(gMaterialData, material_address, dir_in, dir_out, g, dir);
	}
	inline float3 eval_material_emission() { return ::eval_material_emission(gMaterialData, material_address, g); }
	inline float3 eval_material_albedo() { return ::eval_material_albedo(gMaterialData, material_address, g); }
};
inline void make_vertex(const VisibilityInfo v, const RayDifferential ray, out PathVertex r) {
	r.instance_primitive_index = v.data[1].x;
	if (r.instance_index() == -1) {
		r.material_address = gPushConstants.gEnvironmentMaterialAddress;
		r.g.position = r.g.geometry_normal = r.g.shading_normal = r.g.tangent.xyz = (min16float3)ray.direction;
		r.g.tangent.w = 1;
		r.g.d_position.dx = r.g.d_position.dy = 0;
		r.g.shape_area = 0;
		r.g.uv      = (min16float2)cartesian_to_spherical_uv(ray.direction);
		r.g.d_uv.dx = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dx) - r.g.uv;
		r.g.d_uv.dy = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dy) - r.g.uv;
	} else {
		const InstanceData instance = gInstances[r.instance_index()];
		r.material_address = instance.material_address();
		if (r.primitive_index() == -1)
			r.g = make_sphere_geometry(instance.transform, instance.radius(), ray, v.z());
		else
			r.g = make_triangle_geometry(instance.transform, ray, load_tri(gIndices, instance, r.primitive_index()), v.bary());
	}
}
inline void intersect(inout ray_query_t rayQuery, const RayDifferential ray, out PathVertex v) {
	if (do_ray_query(rayQuery, ray)) {
		BF_SET(v.instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		const InstanceData instance = gInstances[v.instance_index()];
		v.material_address = instance.material_address();
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// Sphere
			BF_SET(v.instance_primitive_index, -1, 16, 16);
			v.g = make_sphere_geometry(instance.transform, instance.radius(), ray, rayQuery.CommittedRayT());
		} else {
			// Triangle
			BF_SET(v.instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			v.g = make_triangle_geometry(instance.transform, ray, load_tri(gIndices, instance, v.primitive_index()), rayQuery.CommittedTriangleBarycentrics());
		}
	} else {
		BF_SET(v.instance_primitive_index, -1, 0, 16);
		BF_SET(v.instance_primitive_index, -1, 16, 16);
		v.material_address = gPushConstants.gEnvironmentMaterialAddress;
		v.g.position = v.g.geometry_normal = v.g.shading_normal = v.g.tangent.xyz = (min16float3)ray.direction;
		v.g.tangent.w = 1;
		v.g.d_position.dx = v.g.d_position.dy = 0;
		v.g.shape_area = 0;
		v.g.uv      = (min16float2)cartesian_to_spherical_uv(ray.direction);
		v.g.d_uv.dx = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dx) - v.g.uv;
		v.g.d_uv.dy = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dy) - v.g.uv;
	}
}

#define GROUP_SIZE 8

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void trace_visibility(uint3 index : SV_DispatchThreadID, uint3 group_index : SV_GroupThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;

	const uint s_mem_index = group_index.y*GROUP_SIZE + group_index.x;

	float2 bary = 0;
	float z = 1.#INF;
	float prev_z = 1.#INF; 
	differential d_z = { 0, 0 };
	float2 prev_uv = (index.xy + 0.5 - gViews[viewIndex].image_min)/float2(gViews[viewIndex].image_max - gViews[viewIndex].image_min);

	rng_t rng = { index.xy, gPushConstants.gRandomSeed, 0 };

	ray_query_t rayQuery;
  PathVertex hit;
	intersect(rayQuery, gViews[viewIndex].create_ray(prev_uv), hit);
	if (hit.instance_index() != -1) {
		z = rayQuery.CommittedRayT();
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
			bary = rayQuery.CommittedTriangleBarycentrics();
		else
			bary = hit.g.uv;
		d_z.dx = (min16float)gViews[viewIndex].world_to_camera.transform_vector(hit.g.d_position.dx).z;
		d_z.dy = (min16float)gViews[viewIndex].world_to_camera.transform_vector(hit.g.d_position.dy).z;

		const float3 prevCamPos = tmul(gPrevViews[viewIndex].world_to_camera, gInstances[hit.instance_index()].prev_transform).transform_point(hit.g.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[viewIndex].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	}
	
	store_visibility(index.xy,
									 rng.v,
									 hit.instance_index(),
									 hit.primitive_index(),
									 bary,
									 hit.g.shading_normal,
									 z,
									 prev_z,
									 d_z,
									 prev_uv);
}

#include "../light.hlsli"

struct PathState {
	float3 radiance;
	float3 throughput;
	float eta_scale;
	RayDifferential ray_in;
	PathVertex vertex;

	inline void sample_direct_light(inout ray_query_t rayQuery, inout rng_t rng, const bool apply_mis) {
		if (!(gSampleLights || gSampleBG)) return;

		const LightSampleRecord light_sample = sample_light_or_environment(rng, vertex.g, ray_in);
		if (light_sample.pdf.pdf <= 0) return;

		const BSDFEvalRecord f = vertex.eval_material(-ray_in.direction, light_sample.to_light);
		if (f.pdfW <= 0) return;

		RayDifferential shadowRay;
		shadowRay.origin = ray_offset(vertex.g.position, dot(light_sample.to_light, vertex.g.geometry_normal) < 0 ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
		shadowRay.direction = light_sample.to_light;
		shadowRay.t_min = 0;
		shadowRay.t_max = light_sample.dist*.999;
		shadowRay.dP = vertex.g.d_position;
		shadowRay.dD = ray_in.dD;
		if (do_ray_query(rayQuery, shadowRay))
			return;

		float3 C1 = f.f * light_sample.radiance;
		if (light_sample.pdf.is_solid_angle)
			C1 /= light_sample.pdf.pdf;
		else
			C1 *= light_sample.pdf.G / light_sample.pdf.pdf;

		const float w = apply_mis ? mis_heuristic(light_sample.pdf.solid_angle(), f.pdfW) : 1;
		radiance += throughput * C1 * w;
	}

	inline bool sample_bsdf(inout ray_query_t rayQuery, inout rng_t rng, const bool apply_mis) {
		const BSDFSampleRecord bsdf_sample = vertex.sample_material(float3(rng.next(), rng.next(), rng.next()), -ray_in.direction);
		if (bsdf_sample.eval.pdfW <= 0) {
			return false;
		}

		// modify eta_scale, trace bsdf ray
		RayDifferential bsdf_ray;
		bsdf_ray.origin = ray_offset(vertex.g.position, dot(bsdf_sample.dir_out, vertex.g.geometry_normal) < 0 ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
		bsdf_ray.direction = bsdf_sample.dir_out;
		bsdf_ray.t_min = 0;
		bsdf_ray.t_max = 1.#INF;
		bsdf_ray.dP = vertex.g.d_position;
		if (bsdf_sample.eta == 0) {
			const float3 H = normalize(-ray_in.direction + bsdf_sample.dir_out);
			bsdf_ray.dD = reflect(ray_in.dD, H);
		} else {
			float3 H = normalize(-ray_in.direction + bsdf_sample.dir_out * bsdf_sample.eta);
			if (dot(H, ray_in.direction) > 0) H = -H;
			bsdf_ray.dD = refract(ray_in.dD, H, bsdf_sample.eta);
			eta_scale /= bsdf_sample.eta*bsdf_sample.eta;
		}
		throughput *= bsdf_sample.eval.f / bsdf_sample.eval.pdfW;

		ray_in = bsdf_ray;
		intersect(rayQuery, ray_in, vertex);

		const float3 L = vertex.eval_material_emission();
		if (any(L > 0)) {
			const PDFMeasure pdf = light_sample_pdf(vertex, ray_in, rayQuery.CommittedRayT());
			const float w = (pdf.pdf > 0 && apply_mis) ? mis_heuristic(bsdf_sample.eval.pdfW, pdf.solid_angle()) : 1;
			radiance += throughput * L * w;
		}
		return true;
	}
};

inline float3 trace_path(const uint2 index, const uint s_mem_index, inout ray_query_t rayQuery, inout rng_t rng, const RayDifferential view_ray) {
	PathState state;
	state.radiance = 0;
	state.throughput = 1;
	state.eta_scale = 1;
	state.ray_in = view_ray;
	make_vertex(load_visibility(index), state.ray_in, state.vertex);

	if (gMaxDepth == 0 && gPushConstants.gDirectLightDepth > 0)
		state.sample_direct_light(rayQuery, rng, false);
	
	for (uint bounce_index = 0; bounce_index < gMaxDepth; bounce_index++) {
		if (bounce_index < gPushConstants.gDirectLightDepth)
			state.sample_direct_light(rayQuery, rng, true);

		if (!state.sample_bsdf(rayQuery, rng, bounce_index < gPushConstants.gDirectLightDepth)) break;

		if (state.vertex.instance_index() == -1 || all(state.throughput <= 1e-6)) break;
		
		if (bounce_index >= gPushConstants.gMinDepth) {
			const float l = min(max3(state.throughput) / state.eta_scale, 0.95);
			if (rng.next() > l)
				break;
			state.throughput /= l;
		}
	}
	return state.radiance;
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void trace_indirect(uint3 index : SV_DispatchThreadID, uint3 group_index : SV_GroupThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;

	const uint s_mem_index = group_index.y*8 + group_index.x;

	const RayDifferential view_ray = gViews[viewIndex].create_ray((index.xy + 0.5 - gViews[viewIndex].image_min)/float2(gViews[viewIndex].image_max - gViews[viewIndex].image_min));

	const VisibilityInfo vis = load_visibility(index.xy);
	PathVertex primary_vertex;
	make_vertex(vis, view_ray, primary_vertex);

	if (primary_vertex.material_address == -1) {
		gRadiance[index.xy] = 0;
		gAlbedo[index.xy] = 0;
		return;
	}

	rng_t rng = { vis.rng_seed() };

	float3 radiance = primary_vertex.eval_material_emission();
	gAlbedo[index.xy] = float4(primary_vertex.eval_material_albedo(), 0);

	if (any(gAlbedo[index.xy].rgb > 0) && gSampleCount > 0) {
		ray_query_t rayQuery;

		float3 indirect = 0;
		for (uint i = 0; i < gSampleCount; i++)
			indirect += trace_path(index.xy, s_mem_index, rayQuery, rng, view_ray);
		radiance += indirect / gSampleCount;
	}

	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_DEMODULATE_ALBEDO) {
		const float3 albedo = gAlbedo[index.xy].rgb;
		if (albedo.r > 0) radiance.r /= albedo.r;
		if (albedo.g > 0) radiance.g /= albedo.g;
		if (albedo.b > 0) radiance.b /= albedo.b;
	}

	gRadiance[index.xy] = float4(radiance, gSampleCount);
}
