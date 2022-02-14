#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E trace_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E trace_direct_light
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E trace_path_bounce

#include "../scene.hlsli"

struct PathBounceState {
	uint4 rng;
	float3 ray_origin;
	uint packed_ray_direction;
	uint3 packed_dP;
	uint instance_primitive_index;
	uint3 packed_dD;
	uint pad;
	float2 bary_or_z;
	uint2 packed_throughput;

	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline float3 throughput() { return float3(unpack_f16_2(packed_throughput[0]), f16tof32(packed_throughput[1])); }
	inline float eta_scale() { return f16tof32(packed_throughput[1]>>16); }
	inline RayDifferential ray() {
		RayDifferential r;
		r.origin    = ray_origin;
		r.direction = unpack_normal_octahedron(packed_ray_direction);
		r.t_min = 0;
		r.t_max = 1.#INF;
		r.dP.dx = min16float3(unpack_f16_2(packed_dP[0]), f16tof32(packed_dP[2]));
		r.dP.dy = min16float3(unpack_f16_2(packed_dP[1]), f16tof32(packed_dP[2]>>16));
		r.dD.dx = min16float3(unpack_f16_2(packed_dD[0]), f16tof32(packed_dD[2]));
		r.dD.dy = min16float3(unpack_f16_2(packed_dD[1]), f16tof32(packed_dD[2]>>16));
		return r;
	}
};
inline void store_path_bounce_state(out PathBounceState p, const uint4 rng, const float3 throughput, const float eta_scale, const float2 bary_or_z, const RayDifferential ray, const uint instance_primitive_index) {
	p.rng = rng;
	p.packed_throughput[0] = pack_f16_2(throughput.xy);
	p.packed_throughput[1] = pack_f16_2(float2(throughput.z, eta_scale));
	p.bary_or_z = bary_or_z;
	p.instance_primitive_index = instance_primitive_index;
	p.ray_origin = ray.origin;
	p.packed_ray_direction = pack_normal_octahedron(ray.direction);
	p.packed_dP[0] = pack_f16_2(ray.dP.dx.xy);
	p.packed_dP[1] = pack_f16_2(ray.dP.dy.xy);
	p.packed_dP[2] = pack_f16_2(float2(ray.dP.dx.z, ray.dP.dy.z));
	p.packed_dD[0] = pack_f16_2(ray.dD.dx.xy);
	p.packed_dD[1] = pack_f16_2(ray.dD.dy.xy);
	p.packed_dD[2] = pack_f16_2(float2(ray.dD.dx.z, ray.dD.dy.z));
}

[[vk::constant_id(0)]] const uint gViewCount = 1;
[[vk::constant_id(1)]] uint gSampleCount = 1;

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
RWStructuredBuffer<PathBounceState> gPathStates;
Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] const struct {
	uint gRandomSeed;
	uint gLightCount;
	uint gViewCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;	
	uint gSamplingFlags;
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
inline void make_vertex(const uint instance_primitive_index, const float2 bary_or_z, const RayDifferential ray, out PathVertex r) {
	r.instance_primitive_index = instance_primitive_index;
	if (r.instance_index() == INVALID_INSTANCE) {
		r.material_address = gPushConstants.gEnvironmentMaterialAddress;
		r.g.position = ray.direction;
		r.g.geometry_normal = r.g.shading_normal = r.g.tangent.xyz = (min16float3)ray.direction;
		r.g.tangent.w = 1;
		r.g.d_position.dx = r.g.d_position.dy = 0;
		r.g.shape_area = 0;
		r.g.uv      = (min16float2)cartesian_to_spherical_uv(ray.direction);
		r.g.d_uv.dx = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dx) - r.g.uv;
		r.g.d_uv.dy = (min16float2)cartesian_to_spherical_uv(ray.direction + ray.dD.dy) - r.g.uv;
	} else {
		const InstanceData instance = gInstances[r.instance_index()];
		r.material_address = instance.material_address();
		if (r.primitive_index() == INVALID_PRIMITIVE)
			r.g = make_sphere_geometry(instance.transform, instance.radius(), ray, bary_or_z.x);
		else
			r.g = make_triangle_geometry(instance.transform, ray, load_tri(gIndices, instance, r.primitive_index()), bary_or_z);
	}
}

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
				//PathVertex v;
				//make_vertex(rayQuery.CandidateInstanceIndex()|(rayQuery.CandidatePrimitiveIndex()<<16), rayQuery.CommittedTriangleBarycentrics(), ray, v);
				rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}
	return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
}
inline void intersect(inout ray_query_t rayQuery, const RayDifferential ray, out PathVertex v) {
	if (do_ray_query(rayQuery, ray)) {
		BF_SET(v.instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		const InstanceData instance = gInstances[v.instance_index()];
		v.material_address = instance.material_address();
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// Sphere
			BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
			v.g = make_sphere_geometry(instance.transform, instance.radius(), ray, rayQuery.CommittedRayT());
		} else {
			// Triangle
			BF_SET(v.instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			v.g = make_triangle_geometry(instance.transform, ray, load_tri(gIndices, instance, v.primitive_index()), rayQuery.CommittedTriangleBarycentrics());
		}
	} else {
		BF_SET(v.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
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
void trace_visibility(uint3 index : SV_DispatchThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;

	float2 bary = 0;
	float z = 1.#INF;
	float prev_z = 1.#INF; 
	differential d_z = { 0, 0 };
	float2 prev_uv = (index.xy + 0.5 - gViews[viewIndex].image_min)/float2(gViews[viewIndex].image_max - gViews[viewIndex].image_min);

	rng_t rng = { index.xy, gPushConstants.gRandomSeed, 0 };

	ray_query_t rayQuery;
  PathVertex primary_vertex;
	const RayDifferential view_ray = gViews[viewIndex].create_ray(prev_uv);
	intersect(rayQuery, view_ray, primary_vertex);
	
	gRadiance[index.xy] = float4(primary_vertex.eval_material_emission(), 1);
	gAlbedo[index.xy] = float4(primary_vertex.eval_material_albedo(), 1);

	if (primary_vertex.instance_index() != INVALID_INSTANCE) {
		z = rayQuery.CommittedRayT();
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
			bary = rayQuery.CommittedTriangleBarycentrics();
		else
			bary = primary_vertex.g.uv;
		d_z.dx = (min16float)gViews[viewIndex].world_to_camera.transform_vector(primary_vertex.g.d_position.dx).z;
		d_z.dy = (min16float)gViews[viewIndex].world_to_camera.transform_vector(primary_vertex.g.d_position.dy).z;

		const float3 prevCamPos = tmul(gPrevViews[viewIndex].world_to_camera, gInstances[primary_vertex.instance_index()].prev_transform).transform_point(primary_vertex.g.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[viewIndex].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	}

	store_visibility(index.xy,
									 rng.v,
									 primary_vertex.instance_index(),
									 primary_vertex.primitive_index(),
									 bary,
									 primary_vertex.g.shading_normal,
									 z,
									 prev_z,
									 d_z,
									 prev_uv);

	uint w,h;
	gRadiance.GetDimensions(w,h);
	store_path_bounce_state(gPathStates[index.y*w + index.x],
		rng.v,
		primary_vertex.instance_index() == INVALID_INSTANCE ? 0 : 1, // throughput
		1, // eta_scale
		primary_vertex.primitive_index() == INVALID_PRIMITIVE ? z : bary,
		view_ray,
		primary_vertex.instance_primitive_index);
}

#include "../light.hlsli"

inline float3 sample_direct_light(const PathVertex vertex, const RayDifferential ray_in, inout ray_query_t rayQuery, inout rng_t rng, const bool apply_mis) {
	if (!(gSampleLights || gSampleBG)) return 0;

	const LightSampleRecord light_sample = sample_light_or_environment(rng, vertex.g, ray_in);
	if (light_sample.pdf.pdf <= 0) return 0;

	const BSDFEvalRecord f = vertex.eval_material(-ray_in.direction, light_sample.to_light);
	if (f.pdfW <= 0) return 0;

	RayDifferential shadowRay;
	shadowRay.origin = ray_offset(vertex.g.position, dot(light_sample.to_light, vertex.g.geometry_normal) < 0 ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	shadowRay.direction = light_sample.to_light;
	shadowRay.t_min = 0;
	shadowRay.t_max = light_sample.dist*.999;
	shadowRay.dP = vertex.g.d_position;
	shadowRay.dD = ray_in.dD;
	if (do_ray_query(rayQuery, shadowRay))
		return 0;

	float3 C1 = f.f * light_sample.radiance;
	if (light_sample.pdf.is_solid_angle)
		C1 /= light_sample.pdf.pdf;
	else
		C1 *= light_sample.pdf.G / light_sample.pdf.pdf;

	const float w = apply_mis ? mis_heuristic(light_sample.pdf.solid_angle(), f.pdfW) : 1;
	return C1 * w;
}
inline float3 sample_bsdf(inout PathVertex vertex, inout RayDifferential ray_in, inout ray_query_t rayQuery, inout rng_t rng, inout float3 throughput, inout float eta_scale, const bool apply_mis) {
	const BSDFSampleRecord bsdf_sample = vertex.sample_material(float3(rng.next(), rng.next(), rng.next()), -ray_in.direction);
	if (bsdf_sample.eval.pdfW <= 0) {
		throughput = 0;
		return 0;
	}
	throughput *= bsdf_sample.eval.f / bsdf_sample.eval.pdfW;

	// modify eta_scale, trace bsdf ray
	RayDifferential bsdf_ray;
	bsdf_ray.origin = ray_offset(vertex.g.position, bsdf_sample.eta == 0 ? vertex.g.geometry_normal : -vertex.g.geometry_normal);
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
	ray_in = bsdf_ray;

	intersect(rayQuery, ray_in, vertex);

	const float3 L = vertex.eval_material_emission();
	if (any(L > 0)) {
		const PDFMeasure pdf = light_sample_pdf(vertex, ray_in, rayQuery.CommittedRayT());
		const float w = (pdf.pdf > 0 && apply_mis) ? mis_heuristic(bsdf_sample.eval.pdfW, pdf.solid_angle()) : 1;
		return throughput * L * w;
	}
	return 0;
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void trace_direct_light(uint3 index : SV_DispatchThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	
	uint w,h;
	gRadiance.GetDimensions(w,h);
	w = index.y*w + index.x;
	#define state gPathStates[w]

	float3 throughput = state.throughput();
	if (all(throughput <= 1e-6)) return;

	PathVertex vertex;
	make_vertex(state.instance_primitive_index, state.bary_or_z, state.ray(), vertex);
	rng_t rng = { state.rng };

	ray_query_t rayQuery;

	gRadiance[index.xy].rgb += throughput * sample_direct_light(vertex, state.ray(), rayQuery, rng, true);
}

[numthreads(GROUP_SIZE,GROUP_SIZE,1)]
void trace_path_bounce(uint3 index : SV_DispatchThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	
	uint w,h;
	gRadiance.GetDimensions(w,h);
	w = index.y*w + index.x;
	#define state gPathStates[w]

	float3 throughput = state.throughput();
	if (all(throughput <= 1e-6)) return;

	PathVertex vertex;
	make_vertex(state.instance_primitive_index, state.bary_or_z, state.ray(), vertex);
	rng_t rng = { state.rng };

	RayDifferential ray_in = state.ray();

	ray_query_t rayQuery;

	gRadiance[index.xy].rgb += throughput * sample_direct_light(vertex, ray_in, rayQuery, rng, true);

	float eta_scale = state.eta_scale();
	gRadiance[index.xy].rgb += sample_bsdf(vertex, ray_in, rayQuery, rng, throughput, eta_scale, true);

	float2 bary_or_z = 0;
	if (vertex.instance_index() == INVALID_INSTANCE) {
		throughput = 0;
	} else {
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
			bary_or_z = rayQuery.CommittedTriangleBarycentrics();
		else
			bary_or_z = rayQuery.CommittedRayT();

		if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RR) {
			const float l = min(max3(throughput) / eta_scale, 0.95);
			if (rng.next() > l)
				throughput = 0;
			else
				throughput /= l;
		}
	}

	store_path_bounce_state(state, rng.v, throughput, eta_scale, bary_or_z, ray_in, vertex.instance_primitive_index);

	#undef state
}
