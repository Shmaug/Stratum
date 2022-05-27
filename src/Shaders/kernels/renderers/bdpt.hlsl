#if 0
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E visibility
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_photons
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E path_step
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry path_step
#endif

#include "../../scene.h"
#include "../../bdpt.h"
#include "../../reservoir.h"

#ifdef __SLANG__
#ifndef gSpecializationFlags
#define gSpecializationFlags 0
#endif
#ifndef gDebugMode
#define gDebugMode 0
#endif
[[vk::push_constant]] ConstantBuffer<BDPTPushConstants> gPushConstants;
#else
[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::push_constant]] const BDPTPushConstants gPushConstants;
#endif

static const bool gRemapThreadIndex		 	= (gSpecializationFlags & BDPT_FLAG_REMAP_THREADS);
static const bool gDemodulateAlbedo		 	= (gSpecializationFlags & BDPT_FLAG_DEMODULATE_ALBEDO);
static const bool gUseRayCones 			 	= (gSpecializationFlags & BDPT_FLAG_RAY_CONES);
static const bool gHasEnvironment		 	= (gSpecializationFlags & BDPT_FLAG_HAS_ENVIRONMENT);
static const bool gHasEmissives 		 	= (gSpecializationFlags & BDPT_FLAG_HAS_EMISSIVES);
static const bool gHasMedia 			 	= (gSpecializationFlags & BDPT_FLAG_HAS_MEDIA);
static const bool gUseNEE		 		 	= (gSpecializationFlags & BDPT_FLAG_NEE);
static const bool gUseNEEMIS 				= (gSpecializationFlags & BDPT_FLAG_NEE_MIS);
static const bool gSampleLightPower			= (gSpecializationFlags & BDPT_FLAG_SAMPLE_LIGHT_POWER);
static const bool gUniformSphereSampling 	= (gSpecializationFlags & BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
static const bool gReservoirNEE 			= (gSpecializationFlags & BDPT_FLAG_RESERVOIR_NEE);
static const bool gTraceLight 				= (gSpecializationFlags & BDPT_FLAG_TRACE_LIGHT);
static const bool gConnectToViews 			= (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_VIEWS);
static const bool gConnectToLightPaths		= (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_LIGHT_PATHS);
static const bool gCountRays 			 	= (gSpecializationFlags & BDPT_FLAG_COUNT_RAYS);

static const uint gLightTraceRNGOffset = gTraceLight ? 0xFFFFFF : 0;

#define gOutputExtent					gPushConstants.gOutputExtent
#define gViewCount 						gPushConstants.gViewCount
#define gLightCount 					gPushConstants.gLightCount
#define gLightDistributionPDF			gPushConstants.gLightDistributionPDF
#define gLightDistributionCDF			gPushConstants.gLightDistributionCDF
#define gEnvironmentMaterialAddress 	gPushConstants.gEnvironmentMaterialAddress
#define gEnvironmentSampleProbability 	gPushConstants.gEnvironmentSampleProbability
#define gRandomSeed 					gPushConstants.gRandomSeed
#define gMinPathVertices 				gPushConstants.gMinPathVertices
#define gMaxPathVertices 				gPushConstants.gMaxPathVertices
#define gMaxLightPathVertices 			gPushConstants.gMaxLightPathVertices
#define gMaxNullCollisions 				gPushConstants.gMaxNullCollisions
#define gNEEReservoirSamples 			gPushConstants.gNEEReservoirSamples

#define gRNGsPerVertex (10 + 2*gMaxNullCollisions)

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4

[[vk::binding( 0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding( 1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding( 2,0)]] ByteAddressBuffer gIndices;
[[vk::binding( 3,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding( 4,0)]] StructuredBuffer<TransformData> gInstanceTransforms;
[[vk::binding( 5,0)]] StructuredBuffer<TransformData> gInstanceInverseTransforms;
[[vk::binding( 6,0)]] StructuredBuffer<TransformData> gInstanceMotionTransforms;
[[vk::binding( 7,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding( 8,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding( 9,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(10,0)]] SamplerState gSampler;
[[vk::binding(11,0)]] RWStructuredBuffer<uint> gRayCount;
[[vk::binding(12,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(13 ,0)]] Texture2D<float4> gImages[gImageCount];

[[vk::binding( 0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding( 1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding( 2,1)]] StructuredBuffer<TransformData> gViewTransforms;
[[vk::binding( 3,1)]] StructuredBuffer<TransformData> gInverseViewTransforms;
[[vk::binding( 4,1)]] StructuredBuffer<TransformData> gPrevInverseViewTransforms;
[[vk::binding( 5,1)]] StructuredBuffer<uint> gViewMediumInstances;
[[vk::binding( 6,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding( 7,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding( 8,1)]] RWTexture2D<float4> gDebugImage;
[[vk::binding( 9,1)]] RWStructuredBuffer<VisibilityInfo> gAtomicRadiance;
[[vk::binding(10,1)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(11,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(12,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;
[[vk::binding(13,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(14,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(15,1)]] RWStructuredBuffer<LightPathVertex0> gLightPathVertices0;
[[vk::binding(16,1)]] RWStructuredBuffer<LightPathVertex1> gLightPathVertices1;
[[vk::binding(17,1)]] RWStructuredBuffer<LightPathVertex2> gLightPathVertices2;
[[vk::binding(18,1)]] RWStructuredBuffer<LightPathVertex3> gLightPathVertices3;

#define LIGHT_SAMPLE_QUANTIZATION 1024

#include "../common/rng.hlsli"
typedef uint4 rng_state_t;
rng_state_t rng_init(const uint2 pixel, const uint offset = 0) { return uint4(pixel, gRandomSeed, offset); }
void  rng_skip_next (inout rng_state_t state) { state.w++; }
uint  rng_next_uint (inout rng_state_t state) { return pcg_next_uint(state); }
float rng_next_float(inout rng_state_t state) { return pcg_next_float(state); }

#include "../common/intersection.hlsli"
#include "../../materials/environment.h"
#include "../common/light.hlsli"

#ifdef GROUPSHARED_BETA
// 3 floats
groupshared Spectrum s_beta[GROUPSIZE_X*GROUPSIZE_Y];
#define _beta s_beta[group_thread_index]
#define DECLARE_BETA
#else
#define DECLARE_BETA Spectrum _beta;
#endif

#ifdef GROUPSHARED_MATERIAL
// 11 floats
groupshared Material s_material[GROUPSIZE_X*GROUPSIZE_Y];
#define _material s_material[group_thread_index]
#define DECLARE_MATERIAL
#define DECLARE_MATERIAL_ARG const uint group_thread_index
#define PASS_MATERIAL_ARG group_thread_index
#else
#define DECLARE_MATERIAL Material _material;
#define DECLARE_MATERIAL_ARG inout Material _material
#define PASS_MATERIAL_ARG _material
#endif

uint map_thread_index(const uint2 pixel_coord, const uint2 group_id, const uint group_thread_index) {
	uint path_index;
	if (gRemapThreadIndex) {
		const uint dispatch_w = (gOutputExtent.x + GROUPSIZE_X - 1) / GROUPSIZE_X;
		const uint group_index = group_id.y*dispatch_w + group_id.x;
		path_index = group_index*GROUPSIZE_X*GROUPSIZE_Y + group_thread_index;
	} else
		path_index = pixel_coord.y*gOutputExtent.x + pixel_coord.x;
	return path_index < gOutputExtent.x*gOutputExtent.y ? path_index : -1;
}

Real correct_shading_normal(const Real ndotout, const Real ndotin, const Vector3 dir_out, const Vector3 dir_in, const Vector3 geometry_normal) {
	Real num = ndotout * dot(dir_in, geometry_normal);
	Real denom = dot(dir_out, geometry_normal) * ndotin;
	if (denom == 0) return 0;
	return abs(num / denom);
}

struct NEE {
	Spectrum Le;
	Real T_dir_pdf;
	Vector3 local_to_light;
	Real pdfA; // area measure, includes T_nee_pdf
	Real G;
	Real reservoir_weight;

	static Real mis(const Real a, const Real b) {
		if (!gUseNEEMIS) return 0.5f;
		const Real a2 = pow2(a);
		return a2 / (a2 + pow2(b));
	}
	static Real bsdf_sample_weight(const Real bsdf_pdf, const Real T_nee_pdf, DECLARE_ISECT_ARG, const Real G, const uint path_length) {
		if (gReservoirNEE && path_length == 3) return 0.5;
		Real l_pdf = T_nee_pdf;
		bool area_measure;
		point_on_light_pdf(l_pdf, area_measure, PASS_ISECT_ARG);
		if (!area_measure) l_pdf = pdfWtoA(l_pdf, G);
		return mis(pdfWtoA(bsdf_pdf, G), l_pdf);
	}

	SLANG_MUTATING
	void sample(inout uint4 _rng, const uint cur_medium, DECLARE_ISECT_ARG) {
		reservoir_weight = 0;

		LightSampleRecord ls;
		sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), _isect.sd.position);
		if (ls.pdf <= 0 || all(ls.radiance <= 0)) { Le = 0; return; }

		if (ls.is_environment())
			G = 1;
		else {
			const Real cos_theta = abs(dot(ls.to_light, ls.normal));
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta) / pow2(ls.dist);
		}
		Le = G * ls.radiance;
		pdfA = ls.pdf_area_measure ? ls.pdf : pdfWtoA(ls.pdf, G);

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = ls.to_light;
		} else {
			local_to_light = _isect.sd.to_local(ls.to_light);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
		}

		T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, ls.to_light, ls.dist, cur_medium, Le, T_dir_pdf, pdfA);
		Le /= pdfA;
	}

	static void reservoir_ris(out Reservoir r, BSDF m, inout uint4 _rng, const uint cur_medium, DECLARE_ISECT_ARG) {
		r.init();
		for (uint i = 0; i < gNEEReservoirSamples; i++) {
			LightSampleRecord ls;
			sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), _isect.sd.position);
			if (ls.pdf <= 0 || all(ls.radiance <= 0)) continue;

			Real G;
			if (ls.is_environment())
				G = 1;
			else {
				const Real cos_theta = abs(dot(ls.to_light, ls.normal));
				if (cos_theta < 1e-4) continue;
				G = abs(cos_theta) / pow2(ls.dist);
			}

			Vector3 local_to_light;
			if (gHasMedia && _isect.sd.shape_area == 0)
				local_to_light = ls.to_light;
			else
				local_to_light = _isect.sd.to_local(ls.to_light);

			const Real target_pdf = luminance(ls.radiance)*G;
			if (!ls.pdf_area_measure) ls.pdf = pdfWtoA(ls.pdf, G);

			ReservoirLightSample s;
			s.local_position = ls.local_position;
			s.instance_primitive_index = ls.instance_primitive_index;
			r.update(rng_next_float(_rng), s, ls.pdf, target_pdf);
		}
	}

	SLANG_MUTATING
	void sample_reservoir(inout uint4 _rng, const uint cur_medium, DECLARE_ISECT_ARG, const Reservoir r) {
		reservoir_weight = r.W();
		if (reservoir_weight <= 0) { Le = 0; return; }

		ShadingData sd;
		make_shading_data(sd, r.light_sample.instance_index(), r.light_sample.primitive_index(), r.light_sample.local_position);

		const uint material_address = gInstances[r.light_sample.instance_index()].material_address();
		Material m;
		m.load_and_sample(material_address, sd.uv, 0);
		Le = m.emission;

		pdfA = r.sample_target_pdf;

		Vector3 to_light;
		Real dist;
		if (r.light_sample.instance_index() == INVALID_INSTANCE) {
			to_light = sd.position;
			dist = POS_INFINITY;
			G = 1;
		} else {
			to_light = sd.position - _isect.sd.position;
			dist = length(to_light);
			to_light /= dist;

			const Real cos_theta = abs(dot(to_light, sd.geometry_normal()));
			if (cos_theta < 1e-4) { Le = 0; return; }
			G = abs(cos_theta) / pow2(dist);
			Le *= G;
		}

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = _isect.sd.to_local(to_light);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
		}

		Real T_nee_pdf = 1;
		T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, to_light, dist, cur_medium, Le, T_dir_pdf, T_nee_pdf);
		Le /= T_nee_pdf;
		pdfA *= T_nee_pdf;
	}

	// returns full contribution, ie C*G*f/pdf
	Spectrum eval(const BSDF m, const Vector3 local_dir_in, out Real weight) {
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_light, false);
		weight = reservoir_weight > 0 ? reservoir_weight*0.5 : mis(pdfA, pdfWtoA(T_dir_pdf*_eval.pdf_fwd, G));
		return Le * _eval.f;
	}
};

struct LightPathConnection {
	Spectrum f;
	Real T_dir_pdf;
	Vector3 local_to_light;
	Real G;

	static Real path_weight(const uint path_length) {
		return 1.f / (path_length - 2);
	}

	static void store_light_vertex(const uint path_index, const uint path_length, const Spectrum _beta, DECLARE_ISECT_ARG, const uint packed_local_dir_in, const uint material_address, const float pdf_fwd, const float pdf_rev) {
		const uint i = path_index*gMaxLightPathVertices + path_length-1;
		gLightPathVertices0[i].position = _isect.sd.position;
		gLightPathVertices0[i].packed_geometry_normal = _isect.sd.packed_geometry_normal;
		gLightPathVertices1[i].material_address_flags = material_address;
		gLightPathVertices1[i].packed_local_dir_in = packed_local_dir_in;
		gLightPathVertices1[i].packed_shading_normal = _isect.sd.packed_shading_normal;
		gLightPathVertices1[i].packed_tangent = _isect.sd.packed_tangent;
		gLightPathVertices2[i].uv = _isect.sd.uv;
		gLightPathVertices2[i].pack_beta(_beta);
		gLightPathVertices3[i].pdf_fwd = pdf_fwd;
		gLightPathVertices3[i].pdf_rev = pdf_rev;
	}

	SLANG_MUTATING
	void sample(inout uint4 _rng, const uint path_index, const uint light_length, DECLARE_ISECT_ARG, const uint cur_medium) {
		const uint li = path_index*gMaxLightPathVertices + light_length-1;

		const LightPathVertex2 lv2 = gLightPathVertices2[li];
		f = lv2.beta();
		if (all(f <= 0)) return;

		const LightPathVertex0 lv0 = gLightPathVertices0[li];

		Vector3 origin = _isect.sd.position;
		Vector3 to_light = lv0.position - origin;
		const Real dist = length(to_light);
		to_light /= dist;

		const LightPathVertex1 lv1 = gLightPathVertices1[li];

		MaterialEvalRecord _eval;
		if (lv1.is_material()) {
			Material m;
			m.load_and_sample(lv1.material_address(), gLightPathVertices2[li].uv, 0);
			const float3 local_dir_out = lv1.to_local(-to_light);
			m.eval(_eval, lv1.local_dir_in(), local_dir_out, true);
		} else if (lv1.is_medium()) {
			Medium m;
			m.load(lv1.material_address());
			m.eval(_eval, lv1.local_dir_in(), -to_light);
		}

		if (lv1.is_material()) {
			const Real cos_theta = abs(dot(to_light, lv0.geometry_normal()));
			if (cos_theta < 1e-4) { f = 0; return; }
			G = abs(cos_theta) / pow2(dist);
			f *= G;
		} else
			G = 1;

		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_light = to_light;
		} else {
			local_to_light = _isect.sd.to_local(to_light);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_light.z > 0 ? geometry_normal : -geometry_normal);
		}

		T_dir_pdf = 1;
		Real T_nee_pdf;
		trace_visibility_ray(_rng, origin, to_light, dist, cur_medium, f, T_dir_pdf, T_nee_pdf);
		f /= T_nee_pdf;
	}

	static void connect(inout uint4 _rng, const uint path_index, DECLARE_ISECT_ARG, const uint cur_medium, const BSDF m, const Vector3 local_dir_in, const uint view_length, const Spectrum _beta) {
		LightPathConnection c;
		for (uint light_length = 1; light_length <= gMaxLightPathVertices && light_length + view_length <= gMaxPathVertices; light_length++) {
			c.sample(_rng, path_index, light_length, PASS_ISECT_ARG, cur_medium);

			MaterialEvalRecord _eval;
			m.eval(_eval, local_dir_in, c.local_to_light, false);

			const Spectrum contrib = _beta * c.f * _eval.f;
			const Real weight = path_weight(view_length + light_length);

			if (weight > 0 && any(contrib > 0)) {
				const uint2 pixel_coord = uint2(path_index%gOutputExtent.x, path_index/gOutputExtent.x);
				gRadiance[pixel_coord.xy].rgb += contrib * weight;
				if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution && light_length == gPushConstants.gDebugLightPathLength && view_length == gPushConstants.gDebugViewPathLength)
					gDebugImage[pixel_coord.xy].rgb += contrib;
			}
		}
	}
};

struct ViewConnection {
	Vector3 local_to_view;
	uint output_index;
	Spectrum contribution;

	static Spectrum load_sample(const uint2 pixel_coord) {
		return gLightTraceSamples.Load<uint3>(16 * (pixel_coord.y * gOutputExtent.x + pixel_coord.x))/(float)LIGHT_SAMPLE_QUANTIZATION;
	}
	static void accumulate_contribution(const uint output_index, const Spectrum c) {
		const int3 ci = c * LIGHT_SAMPLE_QUANTIZATION;
		gLightTraceSamples.InterlockedAdd(16*output_index + 0, ci[0]);
		gLightTraceSamples.InterlockedAdd(16*output_index + 4, ci[1]);
		gLightTraceSamples.InterlockedAdd(16*output_index + 8, ci[2]);
	}

	SLANG_MUTATING
	void sample(inout uint4 _rng, const uint cur_medium, DECLARE_ISECT_ARG, const Spectrum beta) {
		uint view_index = 0;
		if (gViewCount > 1) {
			view_index = min(rng_next_float(_rng)*gViewCount, gViewCount-1);
		}

		const Vector3 position = Vector3(
			gViewTransforms[view_index].m[0][3],
			gViewTransforms[view_index].m[1][3],
			gViewTransforms[view_index].m[2][3] );
		const Vector3 normal = normalize(gViewTransforms[view_index].transform_vector(Vector3(0,0,gViews[view_index].projection.near_plane > 0 ? 1 : -1)));

		Vector3 to_view = position - _isect.sd.position;
		const Real dist = length(to_view);
		to_view /= dist;

		const Real sensor_cos_theta = -dot(to_view, normal);
		if (sensor_cos_theta < 0) { contribution = 0; return; }

        const float lens_radius = 0;
        const float lens_area = lens_radius != 0 ? (M_PI * lens_radius * lens_radius) : 1;
		const float sensor_pdf = pow2(dist) / (sensor_cos_theta * lens_area);
        const float sensor_importance = 1 / (gViews[view_index].projection.sensor_area * lens_area * pow4(sensor_cos_theta));

		contribution = beta * sensor_importance / sensor_pdf;

		float4 screen_pos = gViews[view_index].projection.project_point(gInverseViewTransforms[view_index].transform_point(_isect.sd.position));
		screen_pos.y = -screen_pos.y;
		screen_pos.xyz /= screen_pos.w;
		if (any(abs(screen_pos.xyz) >= 1) || screen_pos.z <= 0) { contribution = 0; return; }
        const float2 uv = screen_pos.xy*.5 + .5;
        const int2 ipos = gViews[view_index].image_min + (gViews[view_index].image_max - gViews[view_index].image_min) * uv;
		output_index = ipos.y * gOutputExtent.x + ipos.x;

		Vector3 origin = _isect.sd.position;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_to_view = to_view;
		} else {
			local_to_view = _isect.sd.to_local(to_view);
			const Vector3 geometry_normal = _isect.sd.geometry_normal();
			origin = ray_offset(origin, local_to_view.z > 0 ? geometry_normal : -geometry_normal);
		}

		Real T_nee_pdf = 1;
		Real T_dir_pdf = 1;
		trace_visibility_ray(_rng, origin, to_view, dist, cur_medium, contribution, T_dir_pdf, T_nee_pdf);
		contribution /= T_nee_pdf;
	}

	void eval(const BSDF m, const Vector3 local_dir_in, const uint path_length) {
		MaterialEvalRecord _eval;
		m.eval(_eval, local_dir_in, local_to_view, true);
		if (gConnectToLightPaths) _eval.f *= LightPathConnection::path_weight(path_length + 1);
		accumulate_contribution(output_index, contribution * _eval.f);
	}
};

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_photons(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = pixel_coord.y*gOutputExtent.x + pixel_coord.x;
	uint4 _rng = rng_init(pixel_coord.xy, gLightTraceRNGOffset);

	LightSampleRecord ls;
	sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), 0);
	if (ls.pdf <= 0 || all(ls.radiance <= 0)) gPathStates[path_index].beta = 0;

	const float3 local_dir_out = sample_cos_hemisphere(rng_next_float(_rng), rng_next_float(_rng));
	ls.pdf *= cosine_hemisphere_pdfW(local_dir_out.z);

	float3 T,B;
	make_orthonormal(ls.normal, T, B);
	const float3 dir_out = T*local_dir_out.x + B*local_dir_out.y + ls.normal*local_dir_out.z;

	const float3 origin = ray_offset(ls.position, ls.normal);
	uint _medium = -1;

	DECLARE_BETA
	DECLARE_ISECT
	_beta = ls.radiance;
	float T_dir_pdf = ls.pdf;
	float T_nee_pdf = 1;
	trace_ray(_rng, origin, dir_out, _medium, _beta, T_dir_pdf, T_nee_pdf, PASS_ISECT_ARG);
	_beta /= T_dir_pdf;

	if (_isect.instance_index() == INVALID_INSTANCE) { gPathStates[path_index].beta = 0; return; }

	float3 local_dir_in;
	uint material_address;
	if (gHasMedia && _isect.sd.shape_area == 0) {
		local_dir_in = -dir_out;
		material_address = gInstances[_medium].material_address();
	} else {
		local_dir_in = _isect.sd.to_local(-dir_out);
		material_address = gInstances[_isect.instance_index()].material_address();
	}
	LightPathConnection::store_light_vertex(path_index, 2, _beta, PASS_ISECT_ARG, pack_normal_octahedron(local_dir_in), material_address, ls.pdf, 0);

	// sample next direction
	const float3 rnd = float3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));

	MaterialSampleRecord _material_sample;
	if (gHasMedia && _isect.sd.shape_area == 0) {
		Medium m;
		m.load(gInstances[_medium].material_address());
		if (gMaxLightPathVertices > 2) {
			// add light trace contribution
			if (gConnectToViews) {
				ViewConnection c;
				c.sample(_rng, _medium, PASS_ISECT_ARG, _beta);
				if (any(c.contribution > 0)) c.eval(m, -dir_out, 1);
			}

			// sample direction, contribution is 1 for media (TODO: volume albedo)
			m.sample(_material_sample, rnd, -dir_out, _beta, true);
		}
	} else {
		DECLARE_MATERIAL
		_material.load_and_sample(gInstances[_isect.instance_index()].material_address(), _isect.sd.uv, _isect.sd.uv_screen_size);
		if (gMaxLightPathVertices > 2) {
			// add light trace contribution
			if (gConnectToViews) {
				ViewConnection c;
				c.sample(_rng, _medium, PASS_ISECT_ARG, _beta);
				if (any(c.contribution > 0)) c.eval(PASS_MATERIAL_ARG, local_dir_in, 1);
			}

			// sample direction, apply contribution
			_material.sample(_material_sample, rnd, local_dir_in, _beta, true);
			const float3 geometry_normal = _isect.sd.geometry_normal();
			const float ndotout = _material_sample.dir_out.z;
			_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? geometry_normal : -geometry_normal);
			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);
			_beta *= correct_shading_normal(ndotout, local_dir_in.z, _material_sample.dir_out, -dir_out, geometry_normal);
		}
	}

	if (any(_beta > 0)) {
		gPathStates[path_index].beta = _beta;
		gPathStates[path_index].position = _isect.sd.position;
		gPathStates[path_index].dir_out = _material_sample.dir_out;
		gPathStates[path_index].pack_pdfs(_material_sample.pdf_fwd, _material_sample.pdf_rev);
		gPathStates[path_index].path_length = 2;
		gPathStates[path_index].medium = _medium;
	} else
		gPathStates[path_index].beta = 0;
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void visibility(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint view_index = get_view_index(pixel_coord.xy, gViews, gViewCount);
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (view_index == -1 || path_index == -1) return;

	DECLARE_BETA

	float3 c = 0;
	if (gConnectToViews) {
		c = ViewConnection::load_sample(pixel_coord.xy);
		if (any(isnan(c)) || any(isinf(c)) || any(c < 0)) c = 0;
		if ((DebugMode)gDebugMode == DebugMode::eLightTraceContribution) gDebugImage[pixel_coord.xy] = float4(c, 1);
		if (!gConnectToLightPaths) {
			// average light tracing with regular path tracing
			_beta *= 0.5;
			c *= 0.5;
		}
	}
	gRadiance[pixel_coord.xy] = float4(c,1);

	if (gMaxPathVertices < 2) return;

	// initialize ray

	const float2 uv = (pixel_coord.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_dir_out = normalize(gViews[view_index].projection.back_project(clipPos));
	const float3 dir_out = normalize(gViewTransforms[view_index].transform_vector(local_dir_out));

	uint _medium = gViewMediumInstances[view_index];
	uint4 _rng = rng_init(pixel_coord.xy);
	const float3 origin = float3(
		gViewTransforms[view_index].m[0][3],
		gViewTransforms[view_index].m[1][3],
		gViewTransforms[view_index].m[2][3] );

	// trace visibility ray

	DECLARE_ISECT
	_beta = 1;
	float T_dir_pdf = 1;
	float T_nee_pdf = 1;
	trace_ray(_rng, origin, dir_out, _medium, _beta, T_dir_pdf, T_nee_pdf, PASS_ISECT_ARG);
	_beta /= T_dir_pdf;

	// store visibility
	gVisibility[path_index].position = _isect.sd.position;
	gVisibility[path_index].instance_primitive_index = _isect.instance_primitive_index;
	gVisibility[path_index].packed_normal = _isect.sd.packed_shading_normal;

	// handle miss
	if (_isect.instance_index() == INVALID_INSTANCE) {
		if (gUseNEE && gReservoirNEE) gReservoirs[path_index].init();
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			gRadiance[pixel_coord.xy].rgb += _beta * env.eval(dir_out);
			gAlbedo[pixel_coord.xy] = 1;
		} else {
			gAlbedo[pixel_coord.xy] = float4(0,0,0,1);
		}
		gPathStates[path_index].beta = 0;
		gVisibility[path_index].prev_uv = uv;
		gVisibility[path_index].packed_z = pack_f16_2(POS_INFINITY);
		gVisibility[path_index].packed_dz = pack_f16_2(0);
		return;
	}

	if      ((DebugMode)gDebugMode == DebugMode::eGeometryNormal) gDebugImage[pixel_coord.xy] = float4(_isect.sd.geometry_normal()*.5+.5, 1);
	else if ((DebugMode)gDebugMode == DebugMode::eShadingNormal)  gDebugImage[pixel_coord.xy] = float4(_isect.sd.shading_normal()*.5+.5, 1);

	const float z = length(origin - _isect.sd.position);

	// initialize ray differential
	if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
		RayDifferential ray_differential;
		ray_differential.radius = 0;
		ray_differential.spread = 1 / min(gViews[view_index].extent().x, gViews[view_index].extent().y);
		ray_differential.transfer(z);
		_isect.sd.uv_screen_size *= ray_differential.radius;
		gRayDifferentials[path_index] = ray_differential;
	}

	uint material_address;
	{
		const InstanceData instance = gInstances[_isect.instance_index()];
		material_address = instance.material_address();

		// store visibility packed_dz
		switch (instance.type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			// TODO: figure out dz
			const float3 view_normal = gInverseViewTransforms[view_index].transform_vector(_isect.sd.geometry_normal());
			gVisibility[path_index].packed_dz = pack_f16_2(1/(abs(view_normal.xy) + 1e-2));
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			gVisibility[path_index].packed_dz = pack_f16_2(1/sqrt(instance.radius()));
			break;
		case INSTANCE_TYPE_VOLUME:
			gVisibility[path_index].packed_dz = pack_f16_2(1);
			break;
		}
	}
	// calculate prev_uv
	const float3 prevCamPos = tmul(gPrevInverseViewTransforms[view_index], gInstanceMotionTransforms[_isect.instance_index()]).transform_point(_isect.sd.position);
	gVisibility[path_index].packed_z = pack_f16_2(float2(z, length(prevCamPos)));
	float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
	prevScreenPos.y = -prevScreenPos.y;
	prevScreenPos.xyz /= prevScreenPos.w;
	gVisibility[path_index].prev_uv = prevScreenPos.xy*.5 + .5;

	if (gMaxPathVertices <= 1) return;
	if ((DebugMode)gDebugMode == DebugMode::ePathLengthContribution) gDebugImage[pixel_coord.xy] = float4(0,0,0,1);

	// sample next direction
	const float3 rnd = float3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));

	MaterialSampleRecord _material_sample;
	if (gHasMedia && _isect.sd.shape_area == 0) {
		Medium m;
		m.load(gInstances[_medium].material_address());
		if (gMaxPathVertices > 2) {
			// add nee contribution
			if (gUseNEE) {
				NEE c;
				if (gReservoirNEE) {
					Reservoir r;
					NEE::reservoir_ris(r, m, _rng, _medium, PASS_ISECT_ARG);
					c.sample_reservoir(_rng, _medium, PASS_ISECT_ARG, r);
				} else
					c.sample(_rng, _medium, PASS_ISECT_ARG);
				if (any(c.Le > 0)) {
					Real weight;
					const Spectrum contrib = c.eval(m, -dir_out, weight) * _beta;
					gRadiance[pixel_coord.xy].rgb += contrib * weight;
					if      ((DebugMode)gDebugMode == DebugMode::eNEEContribution)         gDebugImage[pixel_coord.xy] = float4(contrib, 1);
					else if ((DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution) gDebugImage[pixel_coord.xy] = float4(contrib * weight, 1);
				} else if ((DebugMode)gDebugMode == DebugMode::eNEEContribution || (DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution)
					gDebugImage[pixel_coord.xy] = float4(0, 0, 0, 1);
			}

			// add bdpt contribution
			if (gConnectToLightPaths) LightPathConnection::connect(_rng, path_index, PASS_ISECT_ARG, _medium, m, -dir_out, 1, _beta);

			// sample direction (TODO: volume albedo, contribution is 1 for media)
			float3 beta = 1;
			m.sample(_material_sample, rnd, -dir_out, beta, false);
		}
	} else {
		DECLARE_MATERIAL
		_material.load_and_sample(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);

		if (gDemodulateAlbedo) _beta /= _material.diffuse_reflectance;
		gAlbedo[pixel_coord.xy] = float4(_material.diffuse_reflectance, 1);
		gRadiance[pixel_coord.xy].rgb += _beta * _material.emission;

		if (gMaxPathVertices > 2) {
			const float3 local_dir_in = _isect.sd.to_local(-dir_out);

			// add nee contribution
			if (gUseNEE) {
				NEE c;
				if (gReservoirNEE) {
					Reservoir r;
					NEE::reservoir_ris(r, _material, _rng, _medium, PASS_ISECT_ARG);
					c.sample_reservoir(_rng, _medium, PASS_ISECT_ARG, r);
				} else
					c.sample(_rng, _medium, PASS_ISECT_ARG);
				if (any(c.Le > 0)) {
					Real weight;
					const Spectrum contrib = c.eval(PASS_MATERIAL_ARG, local_dir_in, weight) * _beta;
					gRadiance[pixel_coord.xy].rgb += contrib * weight;
					if      ((DebugMode)gDebugMode == DebugMode::eNEEContribution)         gDebugImage[pixel_coord.xy] = float4(contrib, 1);
					else if ((DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution) gDebugImage[pixel_coord.xy] = float4(contrib * weight, 1);
				} else if ((DebugMode)gDebugMode == DebugMode::eNEEContribution || (DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution)
					gDebugImage[pixel_coord.xy] = float4(0, 0, 0, 1);
			}

			// add bdpt contribution
			if (gConnectToLightPaths) LightPathConnection::connect(_rng, path_index, PASS_ISECT_ARG, _medium, _material, local_dir_in, 1, _beta);

			// sample direction, apply contribution
			_material.sample(_material_sample, rnd, local_dir_in, _beta, false);
			const float3 geometry_normal = _isect.sd.geometry_normal();
			_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? geometry_normal : -geometry_normal);
			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);
		}

		if      ((DebugMode)gDebugMode == DebugMode::eAlbedo) 		gDebugImage[pixel_coord.xy] = float4(_material.diffuse_reflectance, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eDiffuse) 		gDebugImage[pixel_coord.xy] = float4(_material.diffuse_reflectance, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eSpecular) 	gDebugImage[pixel_coord.xy] = float4(_material.specular_reflectance, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eTransmission) gDebugImage[pixel_coord.xy] = float4(_material.specular_transmittance.xxx, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eRoughness) 	gDebugImage[pixel_coord.xy] = float4(sqrt(_material.alpha).xxx, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eEmission) 	gDebugImage[pixel_coord.xy] = float4(_material.emission, 1);
	}

	if (any(_beta > 0)) {
		gPathStates[path_index].beta = _beta;
		gPathStates[path_index].position = _isect.sd.position;
		gPathStates[path_index].dir_out = _material_sample.dir_out;
		gPathStates[path_index].pack_pdfs(_material_sample.pdf_fwd, _material_sample.pdf_rev);
		gPathStates[path_index].path_length = 2;
		gPathStates[path_index].medium = _medium;
		if ((DebugMode)gDebugMode == DebugMode::eDirOut) gDebugImage[pixel_coord.xy] = float4(gPathStates[path_index].dir_out*.5+.5, 1);
	} else
		gPathStates[path_index].beta = 0;
}

struct Path {
	uint2 pixel_coord;
	uint path_index;
	uint path_length;
	Spectrum _beta;
	Real _pdf_fwd;
	Real _pdf_rev;

	uint _medium;
	IntersectionVertex _isect;
	Real T_nee_pdf;

	MaterialSampleRecord _material_sample;

	SLANG_MUTATING
	void bounce(BSDF m, inout uint4 _rng, const Vector3 dir_in, const Real dist_in2) {
		Vector3 local_dir_in;
		Real G = 1/dist_in2;
		if (!gHasMedia || _isect.sd.shape_area > 0) {
			local_dir_in = _isect.sd.to_local(dir_in);
			G *= abs(dot(dir_in, _isect.sd.geometry_normal()));
		} else
			local_dir_in = dir_in;

		// add emission from surface
		if (!gTraceLight && any(m.emitted_radiance() > 0)) {
			float w = 1;
			if (gUseNEE) w *= NEE::bsdf_sample_weight(_pdf_fwd, T_nee_pdf, _isect, G, path_length);
			if (gConnectToLightPaths) w *= LightPathConnection::path_weight(path_length);
			gRadiance[pixel_coord].rgb += _beta * m.emitted_radiance() * w;
		}

		if (path_length >= (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices)) { _beta = 0; return; } // dont compute bounce on final vertex

		if (gTraceLight) {
			// add light trace contribution
			if (gConnectToViews) {
				ViewConnection v;
				v.sample(_rng, _medium, _isect, _beta);
				if (any(v.contribution > 0)) v.eval(m, local_dir_in, path_length);
			}
		} else {
			// add nee contribution
			if (gUseNEE) {
				NEE c;
				c.sample(_rng, _medium, _isect);
				if (any(c.Le > 0)) {
					Real weight;
					const Spectrum contrib = c.eval(m, local_dir_in, weight) * _beta;
					gRadiance[pixel_coord].rgb += contrib * weight;
					if      ((DebugMode)gDebugMode == DebugMode::eNEEContribution)         gDebugImage[pixel_coord].rgb += contrib;
					else if ((DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution) gDebugImage[pixel_coord].rgb += contrib * weight;
				}
			}

			// add bdpt contribution
			if (gConnectToLightPaths) LightPathConnection::connect(_rng, path_index, PASS_ISECT_ARG, _medium, m, local_dir_in, path_length, _beta);
		}

		// sample next direction
		const Vector3 material_sample_rnd = Vector3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		m.sample(_material_sample, material_sample_rnd, local_dir_in, _beta, gTraceLight);

		if (!gHasMedia || _isect.sd.shape_area > 0) {
			const float3 geometry_normal = _isect.sd.geometry_normal();

			// offset ray origin
			_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? geometry_normal : -geometry_normal);

			const float ndotout = _material_sample.dir_out.z;
			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);

			if (gTraceLight) _beta *= correct_shading_normal(ndotout, local_dir_in.z, _material_sample.dir_out, dir_in, geometry_normal);
		}
	}
};

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void path_step(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	#define _path_state gPathStates[path_index]

	DECLARE_BETA
	_beta = _path_state.beta;
	if (all(_beta <= 0)) return;

	Path path;
	path.pixel_coord = pixel_coord.xy;
	path.path_index = path_index;
	path.path_length = _path_state.path_length;
	path._beta = _path_state.beta;
	path._medium = _path_state.medium;
	path._pdf_fwd = _path_state.pdf_fwd();
	path._pdf_rev = _path_state.pdf_rev();

	#ifdef BDPT_SINGLE_BOUNCE_KERNEL

	#define TERMINATE_PATH { _path_state.beta = 0; return; }
	#define _position _path_state.position
	#define _dir_out _path_state.dir_out
	#define _pdf_fwd _path_state.pdf_fwd()
	#define _pdf_rev _path_state.pdf_rev()
	#define _medium _path_state.medium
	uint path_length = _path_state.path_length;
	_path_state.path_length = path_length+1;

	#else

	#define TERMINATE_PATH return;
	float3 _position = _path_state.position;
	float3 _dir_out = _path_state.dir_out;
	float _pdf_fwd = _path_state.pdf_fwd();
	float _pdf_rev = _path_state.pdf_rev();
	uint _medium = _path_state.medium;
	for (uint path_length = _path_state.path_length; path_length < (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices);) {

	#endif

		uint4 _rng = rng_init(pixel_coord.xy, gLightTraceRNGOffset + path_length*gRNGsPerVertex);

		// trace bounce ray (sampled at previous vertex)

		DECLARE_ISECT
		float T_dir_pdf = 1;
		float T_nee_pdf = 1;
		trace_ray(_rng, _position, _dir_out, _medium, _beta, T_dir_pdf, T_nee_pdf, PASS_ISECT_ARG);
		if (T_dir_pdf <= 0 || all(_beta <= 0)) TERMINATE_PATH
		_beta /= T_dir_pdf;
		_pdf_fwd *= T_dir_pdf;

		path_length++;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			if (!gTraceLight && gHasEnvironment) {
				Environment env;
				env.load(gEnvironmentMaterialAddress);
				float w = 1;
				if (gUseNEE) w *= NEE::bsdf_sample_weight(_pdf_fwd, T_nee_pdf, PASS_ISECT_ARG, 1, path_length);
				if (gConnectToLightPaths) w *= LightPathConnection::path_weight(path_length);
				gRadiance[pixel_coord.xy].rgb += _beta * env.eval(_dir_out) * w;
			}
			TERMINATE_PATH
		}

		const float3 dp = _position - _isect.sd.position;
		const float dist2 = dot(dp, dp);

		// compute ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(sqrt(dist2));
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}


		// at this point, _dir_out is incoming direction, since we traced the dir_out ray to get here
		float3 local_dir_in;
		uint material_address;
		if (gHasMedia && _isect.sd.shape_area == 0) {
			local_dir_in = -_dir_out;
			material_address = gInstances[_medium].material_address();
		} else {
			local_dir_in = _isect.sd.to_local(-_dir_out);
			material_address = gInstances[_isect.instance_index()].material_address();
		}
		if (gTraceLight) LightPathConnection::store_light_vertex(path_index, 2, _beta, PASS_ISECT_ARG, pack_normal_octahedron(local_dir_in), material_address, _pdf_fwd, _pdf_rev);


		// sample next direction

		const float3 material_sample_rnd = float3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));

		MaterialSampleRecord _material_sample;

		if (gHasMedia && _isect.sd.shape_area == 0) {
			if (path_length >= (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices)) TERMINATE_PATH; // dont compute bounce on final vertex

			Medium m;
			m.load(material_address);

			if (gTraceLight) {
				// add light trace contribution
				if (gConnectToViews) {
					ViewConnection c;
					c.sample(_rng, _medium, PASS_ISECT_ARG, _beta);
					if (any(c.contribution > 0)) c.eval(m, -_dir_out, path_length);
				}
			} else {
				// add nee contribution
				if (gUseNEE) {
					NEE c;
					c.sample(_rng, _medium, PASS_ISECT_ARG);
					if (any(c.Le > 0)) {
						Real weight;
						const Spectrum contrib = c.eval(m, -_dir_out, weight) * _beta;
						gRadiance[pixel_coord.xy].rgb += contrib * weight;
						if      ((DebugMode)gDebugMode == DebugMode::eNEEContribution)         gDebugImage[pixel_coord.xy].rgb += contrib;
						else if ((DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution) gDebugImage[pixel_coord.xy].rgb += contrib * weight;
					}
				}

				// add bdpt contribution
				if (gConnectToLightPaths) LightPathConnection::connect(_rng, path_index, PASS_ISECT_ARG, _medium, m, -_dir_out, path_length, _beta);
			}

			// sample next direction
			m.sample(_material_sample, material_sample_rnd, -_dir_out, _beta);
		} else {
			DECLARE_MATERIAL
			_material.load_and_sample(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);

			// add emission from surface
			if (!gTraceLight && any(_material.emission > 0)) {
				float w = 1;
				if (gUseNEE) w *= NEE::bsdf_sample_weight(_pdf_fwd, T_nee_pdf, PASS_ISECT_ARG, abs(local_dir_in.z)/dist2, path_length);
				if (gConnectToLightPaths) w *= LightPathConnection::path_weight(path_length);
				gRadiance[pixel_coord.xy].rgb += _beta * _material.emission * w;
			}

			if (path_length >= (gTraceLight ? gMaxLightPathVertices : gMaxPathVertices)) TERMINATE_PATH; // dont compute bounce on final vertex

			if (gTraceLight) {
				// add light trace contribution
				if (gConnectToViews) {
					ViewConnection v;
					v.sample(_rng, _medium, PASS_ISECT_ARG, _beta);
					if (any(v.contribution > 0)) v.eval(_material, local_dir_in, path_length);
				}
			} else {
				// add nee contribution
				if (gUseNEE) {
					NEE c;
					c.sample(_rng, _medium, PASS_ISECT_ARG);
					if (any(c.Le > 0)) {
						Real weight;
						const Spectrum contrib = c.eval(PASS_MATERIAL_ARG, local_dir_in, weight) * _beta;
						gRadiance[pixel_coord.xy].rgb += contrib * weight;
						if      ((DebugMode)gDebugMode == DebugMode::eNEEContribution)         gDebugImage[pixel_coord.xy].rgb += contrib;
						else if ((DebugMode)gDebugMode == DebugMode::eWeightedNEEContribution) gDebugImage[pixel_coord.xy].rgb += contrib * weight;
					}
				}

				// add bdpt contribution
				if (gConnectToLightPaths) LightPathConnection::connect(_rng, path_index, PASS_ISECT_ARG, _medium, _material, local_dir_in, path_length, _beta);
			}

			// sample next direction
			_material.sample(_material_sample, material_sample_rnd, local_dir_in, _beta, gTraceLight);

			const float3 geometry_normal = _isect.sd.geometry_normal();

			// offset ray origin
			_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? geometry_normal : -geometry_normal);

			const float ndotout = _material_sample.dir_out.z;
			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);

			if (gTraceLight) _beta *= correct_shading_normal(ndotout, local_dir_in.z, _material_sample.dir_out, -_dir_out, geometry_normal);
		}

		if (all(_beta <= 0)) TERMINATE_PATH

		// russian roullette
		if (path_length >= gMinPathVertices) {
			const float p = min(luminance(_beta), 0.95);
			if (rng_next_float(_rng) > p)
				TERMINATE_PATH
			else
				_beta /= p;
		}

		// store vertex data for next bounce
		_position = _isect.sd.position;
		_dir_out = _material_sample.dir_out;
		#ifdef BDPT_SINGLE_BOUNCE_KERNEL
		_path_state.pack_pdfs(_material_sample.pdf_fwd, _material_sample.pdf_rev);
		_path_state.beta = _beta;
		#else
		_pdf_fwd = _material_sample.pdf_fwd;
		_pdf_rev = _material_sample.pdf_rev;
		#endif

	#ifndef BDPT_SINGLE_BOUNCE_KERNEL
	}
	#endif

	#undef TERMINATE_PATH
}