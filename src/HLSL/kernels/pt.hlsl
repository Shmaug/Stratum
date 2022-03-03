#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_path_bounce

#include "../scene.hlsli"
#include "../tonemap.hlsli"
#include "../reservoir.hlsli"

#define gImageCount 1024

RaytracingAccelerationStructure gScene;
StructuredBuffer<PackedVertexData> gVertices;
ByteAddressBuffer gIndices;
ByteAddressBuffer gMaterialData;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<uint> gLightInstances;

StructuredBuffer<ViewData> gViews;
StructuredBuffer<ViewData> gPrevViews;
StructuredBuffer<uint> gViewMediumIndices;

#include "../visibility_buffer.hlsli"

#include "../reservoir.hlsli"
RWStructuredBuffer<Reservoir> gReservoirs;

RWByteAddressBuffer gCounters;

RWTexture2D<float4> gRadiance;
RWTexture2D<float4> gAlbedo;

StructuredBuffer<float> gDistributions;
SamplerState gSampler;
Texture2D<float4> gImages[gImageCount];

[[vk::push_constant]] const struct {
	uint gRandomSeed;
	uint gLightCount;
	uint gMediumCount;
	uint gViewCount;
	uint gEnvironmentMaterialAddress;
	float gEnvironmentSampleProbability;	
	uint gReservoirSamples;
	uint gSamplingFlags;
	uint gDebugMode;
} gPushConstants;

static const bool gSampleBG     = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_BG_IS)    && gPushConstants.gEnvironmentSampleProbability > 0 && gPushConstants.gEnvironmentMaterialAddress != -1;
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

#include "../image_value.hlsli"

struct PathVertexGeometry {
	float3 position;
	float shape_area;
  float3 geometry_normal;
	float mean_curvature;
  float3 shading_normal;
  float4 tangent;
	float2 uv;
	float inv_uv_size;
	float uv_screen_size;
	
	inline ShadingFrame shading_frame() {
		ShadingFrame frame;
		frame.n = shading_normal;
		frame.t = tangent.xyz;
    frame.b = normalize(cross(frame.n, frame.t))*tangent.w;
		return frame;
	}

	inline float4 sample_image(Texture2D<float4> img) {
		float w,h;
		img.GetDimensions(w,h);
		return img.SampleLevel(gSampler, uv, (gPushConstants.gSamplingFlags & SAMPLE_FLAG_RAY_CONE_LOD) ? log2(max(uv_screen_size*min(w,h), 1e-8f)) : 0);
	}
	
	inline float  eval(const ImageValue1 v) {
		if (!v.has_image()) return v.value;
		return v.value * sample_image(v.image())[v.channel()];
	}
	inline float2 eval(const ImageValue2 v) {
		if (!v.has_image()) return v.value;
		const float4 s = sample_image(v.image());
		return v.value * float2(s[v.channels()[0]], s[v.channels()[1]]);
	}
	inline float3 eval(const ImageValue3 v) {
		if (!v.has_image()) return v.value;
		const float4 s = sample_image(v.image());
		return v.value * float3(s[v.channels()[0]], s[v.channels()[1]], s[v.channels()[2]]);
	}
	inline float4 eval(const ImageValue4 v) {
		if (!v.has_image()) return v.value;
		return v.value * sample_image(v.image());
	}
};
inline PathVertexGeometry make_sphere_geometry(const TransformData transform, const float radius, const float3 position) {
	PathVertexGeometry r;
	r.position = position;
	r.geometry_normal = r.shading_normal = normalize(r.position - transform.transform_point(0));
	r.uv = cartesian_to_spherical_uv(r.geometry_normal);

	const float3 dpdu = transform.transform_vector(float3(-radius * sin(r.uv[0]) * sin(r.uv[1]), 0, radius * cos(r.uv[0]) * sin(r.uv[1])));
	const float3 dpdv = transform.transform_vector(float3( radius * cos(r.uv[0]) * cos(r.uv[1]), -radius * sin(r.uv[1]), radius * sin(r.uv[0]) * cos(r.uv[1])));
	r.tangent = float4(dpdu - r.geometry_normal*dot(r.geometry_normal, dpdu), 1);
	r.shape_area = 4*M_PI*radius*radius;
	r.mean_curvature = 1/radius;
	r.inv_uv_size = (length(dpdu) + length(dpdv)) / 2;
	return r;
}
inline PathVertexGeometry make_triangle_geometry(const TransformData transform, const uint3 tri, const float2 bary) {
	const PackedVertexData v0 = gVertices[tri.x];
	const PackedVertexData v1 = gVertices[tri.y];
	const PackedVertexData v2 = gVertices[tri.z];

	const float3 v1v0 = v1.position - v0.position;
	const float3 v2v0 = v2.position - v0.position;

	PathVertexGeometry r;
	r.position = transform.transform_point(v0.position + v1v0*bary.x + v2v0*bary.y);

	const float3 dPds = transform.transform_vector(-v2v0);
	const float3 dPdt = transform.transform_vector(v1.position - v2.position);
	const float3 ng = cross(dPds, dPdt);
	const float area2 = length(ng);
	r.geometry_normal = ng/area2;
	r.shape_area = area2/2;

	r.uv = v0.uv() + (v1.uv() - v0.uv())*bary.x + (v2.uv() - v0.uv())*bary.y;

	// [du/ds, du/dt]
	// [dv/ds, dv/dt]
	const float2 duvds = v2.uv() - v0.uv();
	const float2 duvdt = v2.uv() - v1.uv();
	// The inverse of this matrix is
	// (1/det) [ dv/dt, -du/dt]
	//         [-dv/ds,  du/ds]
	// where det = duds * dvdt - dudt * dvds
	const float det  = duvds[0] * duvdt[1] - duvdt[0] * duvds[1];
	const float inv_det  = 1/det;
	const float dsdu =  duvdt[1] * inv_det;
	const float dtdu = -duvds[1] * inv_det;
	const float dsdv =  duvdt[0] * inv_det;
	const float dtdv = -duvds[0] * inv_det;
	float3 dPdu,dPdv;
	if (det != 0) {
		// Now we just need to do the matrix multiplication
		dPdu = -(dPds * dsdu + dPdt * dtdu);
		dPdv = -(dPds * dsdv + dPdt * dtdv);
		r.inv_uv_size = max(length(dPdu), length(dPdv));
	} else
		make_orthonormal(r.geometry_normal, dPdu, dPdv);

	r.shading_normal = v0.normal + (v1.normal - v0.normal)*bary.x + (v2.normal - v0.normal)*bary.y;
	if (all(r.shading_normal.xyz == 0) || any(isnan(r.shading_normal))) {
		r.shading_normal = r.geometry_normal;
		r.tangent = float4(dPdu, 1);
		r.mean_curvature = 0;
	} else {
		r.shading_normal = normalize(transform.transform_vector(r.shading_normal));
		if (dot(r.shading_normal, r.geometry_normal) < 0) r.geometry_normal = -r.geometry_normal;
		r.tangent = float4(normalize(dPdu - r.shading_normal*dot(r.shading_normal, dPdu)), 1);
		const float3 dNds = v2.normal - v0.normal;
		const float3 dNdt = v2.normal - v1.normal;
		const float3 dNdu = dNds * dsdu + dNdt * dtdu;
		const float3 dNdv = dNds * dsdv + dNdt * dtdv;
		const float3 bitangent = normalize(cross(r.shading_normal, r.tangent.xyz));
		r.mean_curvature = (dot(dNdu, r.tangent.xyz) + dot(dNdv, bitangent)) / 2;
	}
		
	//if (dot(bitangent, dPdv) < 0) r.tangent.xyz = -r.tangent.xyz;
	return r;
}
inline PathVertexGeometry make_geometry(const uint instance_primitive_index, const float3 position, const float2 bary) {
	if (BF_GET(instance_primitive_index, 0, 16) == INVALID_INSTANCE) {
		PathVertexGeometry r;
		r.position = position;
		r.geometry_normal = r.shading_normal = r.tangent.xyz = position;
		r.tangent.w = 1;
		r.shape_area = 0;
		r.uv = cartesian_to_spherical_uv(position);
		r.mean_curvature = 0;
		r.inv_uv_size = 1;
		return r;
	} else {
		const InstanceData instance = gInstances[BF_GET(instance_primitive_index, 0, 16)];
		if (BF_GET(instance_primitive_index, 16, 16) == INVALID_PRIMITIVE)
			return make_sphere_geometry(instance.transform, instance.radius(), position);
		else
			return make_triangle_geometry(instance.transform, load_tri(gIndices, instance, BF_GET(instance_primitive_index, 16, 16)), bary);
	}
}

struct PathVertex {
	uint instance_primitive_index;
	uint material_address;
	uint medium_index;
	float ray_radius;
	PathVertexGeometry g;
	
	inline uint instance_index() { return BF_GET(instance_primitive_index, 0, 16); }
	inline uint primitive_index() { return BF_GET(instance_primitive_index, 16, 16); }
	inline bool is_surface() { return true; }
};
inline void make_vertex(out PathVertex r, const uint instance_primitive_index, const uint medium_index, const float3 position, const float2 bary, const RayDifferential ray) {
	r.instance_primitive_index = instance_primitive_index;
	r.medium_index = medium_index;
	r.g = make_geometry(instance_primitive_index, position, bary);
	if (r.instance_index() == INVALID_INSTANCE) {
		r.material_address = gPushConstants.gEnvironmentMaterialAddress;
		r.ray_radius = ray.radius;
	} else {
		r.material_address = gInstances[r.instance_index()].material_address();
		r.ray_radius = ray.differential_transfer(length(r.g.position - ray.origin));
	}
	r.g.uv_screen_size = r.ray_radius / r.g.inv_uv_size;
}

#include "../material.hlsli"
#include "../light.hlsli"

#define ray_query_t RayQuery<RAY_FLAG_FORCE_OPAQUE>
inline bool do_ray_query(inout ray_query_t rayQuery, const RayDifferential ray) {
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
							const float t = st.x < 0 ? st.y : st.x;
							if (t <= rayQuery.CommittedRayT() && t >= rayQuery.RayTMin())
								rayQuery.CommitProceduralPrimitiveHit(t);
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
	if (do_ray_query(rayQuery, ray)) {
		// hit an instance
		BF_SET(v.instance_primitive_index, rayQuery.CommittedInstanceID(), 0, 16);
		const InstanceData instance = gInstances[v.instance_index()];
		v.material_address = instance.material_address();
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// sphere
			BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
			switch (instance.type()) {
				case INSTANCE_TYPE_SPHERE:
					v.g = make_sphere_geometry(instance.transform, instance.radius(), ray.origin + ray.direction*rayQuery.CommittedRayT());
					break;
			}
		} else {
			// triangle
			BF_SET(v.instance_primitive_index, rayQuery.CommittedPrimitiveIndex(), 16, 16);
			v.g = make_triangle_geometry(instance.transform, load_tri(gIndices, instance, v.primitive_index()), rayQuery.CommittedTriangleBarycentrics());
		}
		v.ray_radius = ray.differential_transfer(rayQuery.CommittedRayT());
	} else {
		// background
		BF_SET(v.instance_primitive_index, INVALID_INSTANCE, 0, 16);
		BF_SET(v.instance_primitive_index, INVALID_PRIMITIVE, 16, 16);
		v.material_address = gPushConstants.gEnvironmentMaterialAddress;
		v.g.position = ray.direction;
		v.g.geometry_normal = v.g.shading_normal = v.g.tangent.xyz = ray.direction;
		v.g.tangent.w = 1;
		v.g.shape_area = 0;
		v.g.uv = cartesian_to_spherical_uv(ray.direction);
		v.g.mean_curvature = 1;
		v.g.inv_uv_size = 1;
		v.ray_radius = ray.radius;
	}
	v.g.uv_screen_size = v.ray_radius / v.g.inv_uv_size;
}

inline float3 transmittance_along_ray(inout ray_query_t rayQuery, const RayDifferential ray, const uint medium_index) {
	if (do_ray_query(rayQuery, ray)) {

		return 0;
	} else
		return 1;
}

inline float3 sample_direct_light(const PathVertex vertex, const RayDifferential ray_in, inout ray_query_t rayQuery, inout rng_t rng) {
	const LightSampleRecord light_sample = sample_light_or_environment(rng, vertex.g, ray_in);
	if (light_sample.pdf.pdf <= 0) return float3(1,0,1);

	BSDFEvalRecord<float> f;
	if (vertex.is_surface()) {
		f = eval_material<float, true>(gMaterialData, vertex.material_address, -ray_in.direction, light_sample.to_light, vertex.g);
	} else {
		// TODO: sample phase function
	}
	if (f.pdfW <= 0) return 0;

	float3 C1 = f.f * light_sample.radiance / light_sample.pdf.pdf;
	if (!light_sample.pdf.is_solid_angle)
		C1 *= light_sample.pdf.G;

	const bool inside = dot(light_sample.to_light, vertex.g.geometry_normal) < 0;

	RayDifferential shadowRay;
	shadowRay.origin = ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	shadowRay.direction = light_sample.to_light;
	shadowRay.t_min = 0;
	shadowRay.t_max = light_sample.dist*.999;
	shadowRay.radius = ray_in.radius;
	shadowRay.spread = ray_in.spread;
	const float3 T = transmittance_along_ray(rayQuery, shadowRay, vertex.medium_index); // TODO: set to inner_medium if inside
	if (all(T <= 0)) return 0;

	const float w = (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) ? mis_heuristic(light_sample.pdf.solid_angle(), f.pdfW) : 0.5;
	return T * C1 * w;
}
inline float3 sample_reservoir(const PathVertex vertex, const RayDifferential ray_in, inout ray_query_t rayQuery, const uint index_1d) {
 const Reservoir r = gReservoirs[index_1d];

	const PathVertexGeometry light_vertex = make_geometry(r.light_sample.instance_primitive_index, r.light_sample.position_or_bary, r.light_sample.position_or_bary.xy);

	float3 to_light = light_vertex.position - vertex.g.position;
	const float dist = length(to_light);
	const float rcp_dist = 1/dist;
	to_light *= rcp_dist;
	
	BSDFEvalRecord<float> f;
	if (vertex.is_surface()) {
		f = eval_material<float, true>(gMaterialData, vertex.material_address, -ray_in.direction, to_light, vertex.g);
	} else {
		// TODO: sample phase function
	}
	if (f.pdfW <= 0) return 0;
	
	const bool inside = dot(to_light, vertex.g.geometry_normal) < 0;

	RayDifferential shadowRay;
	shadowRay.origin = ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	shadowRay.direction = to_light;
	shadowRay.t_min = 0;
	shadowRay.t_max = dist*.999;
	shadowRay.radius = ray_in.radius;
	shadowRay.spread = ray_in.spread;
	const float3 T = transmittance_along_ray(rayQuery, shadowRay, vertex.medium_index); // TODO: set to inner_medium if inside
	if (all(T <= 0)) return 0;

	const uint light_material_address = r.light_sample.instance_index() == INVALID_INSTANCE ?
		gPushConstants.gEnvironmentMaterialAddress : gInstances[r.light_sample.instance_index()].material_address();
	const float3 L = eval_material_emission<float>(gMaterialData, light_material_address, light_vertex);
	const float G = abs(dot(light_vertex.geometry_normal, to_light)) * (rcp_dist*rcp_dist);
	float w;
	if (gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) {
		PathVertex lv;
		lv.instance_primitive_index = r.light_sample.instance_primitive_index;
		lv.material_address = light_material_address;
		lv.ray_radius = shadowRay.differential_transfer(dist);
		lv.g = light_vertex;
		const PDFMeasure<float> pdf = light_sample_pdf(lv, shadowRay, dist);
		w = mis_heuristic(pdf.solid_angle(), f.pdfW);
	} else
		w = 0.5;
	return T * f.f * L * G * r.W() * w;
}
inline float3 sample_indirect_light(inout PathVertex vertex, inout RayDifferential ray, inout ray_query_t rayQuery, inout rng_t rng, const uint index_1d) {
	const float3 dir_in = -ray.direction;
	
	bool has_bsdf = true;
	
	if (vertex.is_surface()) {
		const BSDFSampleRecord<float> bsdf_sample = sample_material<float, true>(gMaterialData, vertex.material_address, float3(rng.next(), rng.next(), rng.next()), dir_in, vertex.g);
		if (bsdf_sample.eval.pdfW <= 0) {
			gPathStates[index_1d].throughput = 0;
			return 0;
		}
		gPathStates[index_1d].dir_pdf = bsdf_sample.eval.pdfW;
		gPathStates[index_1d].throughput *= bsdf_sample.eval.f / bsdf_sample.eval.pdfW;

		ray.direction = bsdf_sample.dir_out;
		if (bsdf_sample.eta == 0) {
			ray.differential_reflect(vertex.g.mean_curvature, bsdf_sample.roughness);
		} else {
			ray.differential_refract(vertex.g.mean_curvature, bsdf_sample.roughness, bsdf_sample.eta);
			gPathStates[index_1d].eta_scale /= bsdf_sample.eta*bsdf_sample.eta;
		}
	} else {
		// TODO: sample phase function, update throughput, dir_pdf & ray
	}

	const bool inside = dot(ray.direction, vertex.g.geometry_normal) < 0;
	ray.origin = ray_offset(vertex.g.position, inside ? -vertex.g.geometry_normal : vertex.g.geometry_normal);
	intersect(rayQuery, ray, vertex);
	float hit_t = rayQuery.CommittedRayT();
	
	// TODO: keep tracing through medium boundaries, update hit_t & medium_index

	if (gPathStates[index_1d].medium_index < gPushConstants.gMediumCount) {
		bool scatter = false;
		// TODO: compute transmittance & scattering up to vertex
		if (scatter) {
			//hit_t = t;
			//gPathStates[index_1d].medium_index = inner_medium;
		}
	}
	
	if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		gPathStates[index_1d].bary = rayQuery.CommittedTriangleBarycentrics();
	
	gPathStates[index_1d].position = vertex.g.position;
	gPathStates[index_1d].ray_origin = ray.origin;
	gPathStates[index_1d].radius_spread = pack_f16_2(float2(vertex.ray_radius, ray.spread));
	gPathStates[index_1d].instance_primitive_index = vertex.instance_primitive_index;
	
	const float3 L = eval_material_emission<float>(gMaterialData, vertex.material_address, vertex.g);
	if (any(L > 0)) {
		float w = 1;
		if (gSampleLights || gSampleBG) {
			w = 0.5;
			if ((gPushConstants.gSamplingFlags & SAMPLE_FLAG_MIS) && gPathStates[index_1d].dir_pdf > 0) {
				const PDFMeasure<float> pdf = light_sample_pdf(vertex, ray, hit_t);
				if (pdf.pdf > 0) {
					//if (gPushConstants.gReservoirSamples > 0)
					//	w = mis_heuristic(gPathStates[index_1d].dir_pdf, 1/gReservoirs[index_1d].W());
					//else
						w = mis_heuristic(gPathStates[index_1d].dir_pdf, pdf.solid_angle());
				}
			}
		}
		return gPathStates[index_1d].throughput * L * w;
	}
	
	return 0;
}

#include "../autodiff.hlsli"

#define GROUP_SIZE 8

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void trace_visibility(uint3 index : SV_DispatchThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	const float2 view_size = float2(gViews[viewIndex].image_max - gViews[viewIndex].image_min);

	float2 bary = 0;
	float z = 1.#INF;
	float prev_z = 1.#INF; 
	float2 dz = 0;
	float2 prev_uv = (index.xy + 0.5 - gViews[viewIndex].image_min)/view_size;

	rng_t rng = { index.xy, gPushConstants.gRandomSeed, 0 };

	ray_query_t rayQuery;
  PathVertex primary_vertex;
	primary_vertex.medium_index = gViewMediumIndices[viewIndex];

	const RayDifferential view_ray = gViews[viewIndex].create_ray(prev_uv, view_size);
	intersect(rayQuery, view_ray, primary_vertex);


	float3 transmittance = 1;
	// TODO: keep tracing through medium boundaries, update transmittance & medium_index

	gRadiance[index.xy] = float4(transmittance * eval_material_emission<float>(gMaterialData, primary_vertex.material_address, primary_vertex.g), 1);
	gAlbedo  [index.xy] = float4(transmittance *   eval_material_albedo<float>(gMaterialData, primary_vertex.material_address, primary_vertex.g), 1);

	if (primary_vertex.instance_index() != INVALID_INSTANCE) {
		z = rayQuery.CommittedRayT();
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
			bary = rayQuery.CommittedTriangleBarycentrics();
			const float3 view_normal = gViews[viewIndex].world_to_camera.transform_vector(primary_vertex.g.geometry_normal);
			// TODO: figure out dz
			dz.x = 1;//1/(abs(view_normal.x) + 1e-2);
			dz.y = 1;//1/(abs(view_normal.y) + 1e-2);
		} else {
			bary = primary_vertex.g.uv;
			const float radius = gInstances[primary_vertex.instance_index()].radius();
			dz = 1/sqrt(radius);
		}

		const float3 prevCamPos = tmul(gPrevViews[viewIndex].world_to_camera, gInstances[primary_vertex.instance_index()].prev_transform).transform_point(primary_vertex.g.position);
		prev_z = length(prevCamPos);
		float4 prevScreenPos = gPrevViews[viewIndex].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prev_uv = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	} else
		transmittance = 0;

	switch (gPushConstants.gDebugMode) {
		default:
			break;
		case DebugMode::eZ:
			gRadiance[index.xy].rgb = viridis_quintic(1 - exp(-0.1*z));
			break;
		case DebugMode::eDz:
			gRadiance[index.xy].rgb = viridis_quintic(length(dz));
			break;
		case DebugMode::eShadingNormal:
			gRadiance[index.xy].rgb = primary_vertex.g.shading_normal*.5 + .5;
			break;
		case DebugMode::eGeometryNormal:
			gRadiance[index.xy].rgb = primary_vertex.g.geometry_normal*.5 + .5;
			break;
		case DebugMode::eTangent:
			gRadiance[index.xy].rgb = primary_vertex.g.tangent.xyz*.5 + .5;
			break;
		case DebugMode::eMeanCurvature:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(primary_vertex.g.mean_curvature));
			break;
		case eRayRadius:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(100*primary_vertex.ray_radius));
			break;
		case eUVScreenSize:
			gRadiance[index.xy].rgb = viridis_quintic(saturate(log2(1024*primary_vertex.g.uv_screen_size)/10));
			break;
		case DebugMode::ePrevUV:
			gRadiance[index.xy].rgb = float3(prev_uv, 0);
			break;
	}

	store_visibility(index.xy,
									 rng.v,
									 primary_vertex.instance_index(),
									 primary_vertex.primitive_index(),
									 bary, primary_vertex.g.shading_normal,
									 z, prev_z, dz,
									 prev_uv);

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;
	gPathStates[index_1d].rng = rng.v;
	gPathStates[index_1d].throughput = transmittance;
	gPathStates[index_1d].eta_scale = 1;
	gPathStates[index_1d].position = primary_vertex.g.position;
	gPathStates[index_1d].bary = bary;
	gPathStates[index_1d].ray_origin = view_ray.origin;
	gPathStates[index_1d].radius_spread = pack_f16_2(float2(primary_vertex.ray_radius, view_ray.spread));
	gPathStates[index_1d].instance_primitive_index = primary_vertex.instance_primitive_index;
	gPathStates[index_1d].medium_index = primary_vertex.medium_index;

	if (gPushConstants.gReservoirSamples > 0 && (gSampleLights || gSampleBG) && primary_vertex.instance_index() != INVALID_INSTANCE) {
		Reservoir r = init_reservoir();
		for (uint i = 0; i < gPushConstants.gReservoirSamples; i++) {
			const LightSampleRecord light_sample = sample_light_or_environment(rng, primary_vertex.g, view_ray);
			if (light_sample.pdf.pdf <= 0) continue;
			const BSDFEvalRecord<float> f = eval_material<float, true>(gMaterialData, primary_vertex.material_address, -view_ray.direction, light_sample.to_light, primary_vertex.g);
			if (f.pdfW <= 0) continue;
			const ReservoirLightSample s = { light_sample.position_or_bary, light_sample.instance_primitive_index };
			r.update(rng.next(), s, light_sample.pdf.pdf * light_sample.pdf.G, luminance(f.f * light_sample.radiance) * light_sample.pdf.G);
		}
		gReservoirs[index_1d] = r;
	}
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void trace_path_bounce(uint3 index : SV_DispatchThreadID) {
	const uint viewIndex = get_view_index(index.xy, gViews, gPushConstants.gViewCount);
	if (viewIndex == -1) return;
	
	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	if (all(gPathStates[index_1d].throughput <= 1e-6)) return;

	RayDifferential ray_in = gPathStates[index_1d].ray();
	PathVertex vertex;
	make_vertex(vertex, gPathStates[index_1d].instance_primitive_index, gPathStates[index_1d].medium_index, gPathStates[index_1d].position, gPathStates[index_1d].bary, ray_in);

	rng_t rng = { gPathStates[index_1d].rng };

	ray_query_t rayQuery;

	if (gSampleLights || gSampleBG) {
		if (gPushConstants.gReservoirSamples > 0)
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_reservoir(vertex, ray_in, rayQuery, index_1d);
		else
			gRadiance[index.xy].rgb += gPathStates[index_1d].throughput * sample_direct_light(vertex, ray_in, rayQuery, rng);
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
	
	gPathStates[index_1d].rng = rng.v;
}
