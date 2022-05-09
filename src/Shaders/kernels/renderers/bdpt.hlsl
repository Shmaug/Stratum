#if 0
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E visibility
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E path_step
//#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry visibility
//#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry path_step
#endif

#include "../../scene.h"
#include "../../bdpt.h"


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

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4

//#define GROUPSHARED_BETA
//#define GROUPSHARED_TRANSMITTANCE
//#define GROUPSHARED_ISECT
//#define GROUPSHARED_MATERIAL
//#define GROUPSHARED_MATERIAL_SAMPLE

static const bool gRemapThreadIndex		 	= (gSpecializationFlags & BDPT_FLAG_REMAP_THREADS);
static const bool gDemodulateAlbedo		 	= (gSpecializationFlags & BDPT_FLAG_DEMODULATE_ALBEDO);
static const bool gUseRayCones 			 	= (gSpecializationFlags & BDPT_FLAG_RAY_CONES);
static const bool gHasEnvironment		 	= (gSpecializationFlags & BDPT_FLAG_HAS_ENVIRONMENT);
static const bool gHasEmissives 		 	= (gSpecializationFlags & BDPT_FLAG_HAS_EMISSIVES);
static const bool gHasMedia 			 	= (gSpecializationFlags & BDPT_FLAG_HAS_MEDIA);
static const bool gUseNEE		 		 	= (gSpecializationFlags & BDPT_FLAG_NEE);
static const bool gUseMIS 				 	= (gSpecializationFlags & BDPT_FLAG_MIS);
static const bool gUniformSphereSampling 	= (gSpecializationFlags & BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
static const bool gCountRays 			 	= (gSpecializationFlags & BDPT_FLAG_COUNT_RAYS);

#define gViewCount 						gPushConstants.gViewCount
#define gLightCount 					gPushConstants.gLightCount
#define gEnvironmentMaterialAddress 	gPushConstants.gEnvironmentMaterialAddress
#define gEnvironmentSampleProbability 	gPushConstants.gEnvironmentSampleProbability
#define gRandomSeed 					gPushConstants.gRandomSeed
#define gMaxPathVertices 				gPushConstants.gMaxPathVertices
#define gMinPathVertices 				gPushConstants.gMinPathVertices
#define gMaxNullCollisions 				gPushConstants.gMaxNullCollisions

[[vk::binding( 0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding( 1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding( 2,0)]] ByteAddressBuffer gIndices;
[[vk::binding( 3,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding( 4,0)]] StructuredBuffer<TransformData> gInstanceTransforms;
[[vk::binding( 5,0)]] StructuredBuffer<TransformData> gInstanceInverseTransforms;
[[vk::binding( 6,0)]] StructuredBuffer<TransformData> gInstanceMotionTransforms;
[[vk::binding( 7,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding( 8,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding( 9,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(10,0)]] SamplerState gSampler;
[[vk::binding(11,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(12,0)]] Texture2D<float4> gImages[gImageCount];
[[vk::binding(13,0)]] RWStructuredBuffer<uint> gRayCount;

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
[[vk::binding(11,1)]] RWStructuredBuffer<uint4> gRNGStates;
[[vk::binding(12,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(13,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;


#include "../common/rng.hlsli"
typedef uint4 rng_state_t;
uint  rng_next_uint (inout rng_state_t state) { return pcg_next_uint(state); }
float rng_next_float(inout rng_state_t state) { return pcg_next_float(state); }

#include "../common/intersection.hlsli"
#include "../../materials/environment.h"

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
#else
#define DECLARE_MATERIAL Material _material;
#endif

#ifdef GROUPSHARED_MATERIAL_SAMPLE
// 7 floats
groupshared MaterialSampleRecord s_material_sample[GROUPSIZE_X*GROUPSIZE_Y];
#define _material_sample s_material_sample[group_thread_index]
#define DECLARE_MATERIAL_SAMPLE
#else
#define DECLARE_MATERIAL_SAMPLE MaterialSampleRecord _material_sample;
#endif

inline uint map_thread_index(const uint2 pixel_coord, const uint2 group_id, const uint group_thread_index) {
	uint w,h;
	gRadiance.GetDimensions(w,h);
	uint path_index;
	if (gRemapThreadIndex) {
		const uint dispatch_w = (w + GROUPSIZE_X - 1) / GROUPSIZE_X;
		const uint group_index = group_id.y*dispatch_w + group_id.x;
		path_index = group_index*GROUPSIZE_X*GROUPSIZE_Y + group_thread_index;
	} else
		path_index = pixel_coord.y*w + pixel_coord.x;
	return path_index < w*h ? path_index : -1;
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void visibility(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint view_index = get_view_index(pixel_coord.xy, gViews, gViewCount);
	if (view_index == -1) return;

	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);

	const float2 uv = (pixel_coord.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_dir_out = normalize(gViews[view_index].projection.back_project(clipPos));
	const float3 dir_out = normalize(gViewTransforms[view_index].transform_vector(local_dir_out));

	gPathStates[path_index].medium = gViewMediumInstances[view_index];
	uint4 rng = uint4(pixel_coord.xy, gRandomSeed, 0);
	const float3 origin = gViewTransforms[view_index].transform_point(0);

	DECLARE_BETA
	_beta = 1;
	DECLARE_ISECT
	float T_dir_pdf = 1;
	float T_nee_pdf = 1;
	trace_ray(rng, origin, dir_out, gPathStates[path_index].medium, _beta, T_dir_pdf, T_nee_pdf, PASS_ISECT_ARG);
	_beta /= T_dir_pdf;

	gVisibility[path_index].position = _isect.sd.position;
	gVisibility[path_index].instance_primitive_index = _isect.instance_primitive_index;
	gVisibility[path_index].packed_normal = _isect.sd.packed_shading_normal;

	if (_isect.instance_index() != INVALID_INSTANCE) {
		if ((DebugMode)gDebugMode == DebugMode::eGeometryNormal)     gDebugImage[pixel_coord.xy] = float4(_isect.sd.geometry_normal()*.5+.5, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eShadingNormal) gDebugImage[pixel_coord.xy] = float4(_isect.sd.shading_normal()*.5+.5, 1);

		const float z = length(origin - _isect.sd.position);
		// update ray differentials
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

		const float3 prevCamPos = tmul(gPrevInverseViewTransforms[view_index], gInstanceMotionTransforms[_isect.instance_index()]).transform_point(_isect.sd.position);
		gVisibility[path_index].packed_z = pack_f16_2(float2(z, length(prevCamPos)));
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevScreenPos.xyz /= prevScreenPos.w;
		gVisibility[path_index].prev_uv = prevScreenPos.xy*.5 + .5;

		if (gMaxPathVertices > 1) {
			// sample next direction
			const float3 rnd = float3(rng_next_float(rng), rng_next_float(rng), rng_next_float(rng));
			gRNGStates[path_index] = rng;
			DECLARE_MATERIAL_SAMPLE
			if (gHasMedia && _isect.sd.shape_area == 0) {
				Medium m;
				m.load(gInstances[gPathStates[path_index].medium].material_address());
				m.sample(_material_sample, rnd, -dir_out);
			} else {
				DECLARE_MATERIAL
				_material.load_and_sample(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);

				gRadiance[pixel_coord.xy] = float4(_beta * _material.emission, 1);
				gAlbedo[pixel_coord.xy] = float4(_material.color, 1);
				if ((DebugMode)gDebugMode == DebugMode::eAlbedo) gDebugImage[pixel_coord.xy] = float4(_material.color, 1);
				else if ((DebugMode)gDebugMode == DebugMode::eDiffuse) gDebugImage[pixel_coord.xy] = float4(_material.diffuse_reflectance.xxx, 1);
				else if ((DebugMode)gDebugMode == DebugMode::eSpecular) gDebugImage[pixel_coord.xy] = float4(_material.specular_reflectance.xxx, 1);
				else if ((DebugMode)gDebugMode == DebugMode::eTransmission) gDebugImage[pixel_coord.xy] = float4(_material.specular_transmittance.xxx, 1);
				else if ((DebugMode)gDebugMode == DebugMode::eRoughness) gDebugImage[pixel_coord.xy] = float4(sqrt(_material.alpha).xxx, 1);
				else if ((DebugMode)gDebugMode == DebugMode::eEmission) gDebugImage[pixel_coord.xy] = float4(_material.emission, 1);

				_material.sample(_material_sample, rnd, _isect.sd.to_local(-dir_out), _beta, false);
				_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? _isect.sd.geometry_normal() : -_isect.sd.geometry_normal());
				_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);
				if (gDemodulateAlbedo) gRadiance[pixel_coord.xy].rgb /= _material.color;
			}

			if (any(_beta > 0)) {
				gPathStates[path_index].beta = _beta;
				gPathStates[path_index].packed_pixel_coord = (pixel_coord.x&0xFFFF) | pixel_coord.y<<16;
				gPathStates[path_index].position = _isect.sd.position;
				gPathStates[path_index].dir_out = _material_sample.dir_out;
				gPathStates[path_index].path_length = 2;
				if ((DebugMode)gDebugMode == DebugMode::eDirOut) gDebugImage[pixel_coord.xy] = float4(gPathStates[path_index].dir_out*.5+.5, 1);
				if ((DebugMode)gDebugMode == DebugMode::eDirOutPdf) gDebugImage[pixel_coord.xy] = float4(_material_sample.pdf_fwd.xxx, 1);
			} else
				gPathStates[path_index].beta = 0;
		}
	} else {
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			gRadiance[pixel_coord.xy] = float4(_beta * env.eval(dir_out), 1);
			gAlbedo[pixel_coord.xy] = 1;
		} else {
			gRadiance[pixel_coord.xy] = float4(0,0,0,1);
			gAlbedo[pixel_coord.xy] = float4(0,0,0,1);
		}
		gPathStates[path_index].beta = 0;
		gVisibility[path_index].prev_uv = uv;
		gVisibility[path_index].packed_z = pack_f16_2(POS_INFINITY);
		gVisibility[path_index].packed_dz = pack_f16_2(0);
	}
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void path_step(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	DECLARE_BETA
	_beta = gPathStates[path_index].beta;
	if (all(_beta <= 0)) return;

	#ifdef BDPT_SINGLE_BOUNCE_KERNEL

	#define TERMINATE_PATH { gPathStates[path_index].beta = 0; return; }
	#define _position gPathStates[path_index].position
	#define _dir_out gPathStates[path_index].dir_out
	#define _medium gPathStates[path_index].medium
	const uint path_length = gPathStates[path_index].path_length + 1;
	if (path_length > gMaxPathVertices) return;
	gPathStates[path_index].path_length = path_length;

	#else

	#define TERMINATE_PATH return;
	float3 _position = gPathStates[path_index].position;
	float3 _dir_out = gPathStates[path_index].dir_out;
	uint _medium = gPathStates[path_index].medium;
	uint4 _rng = gRNGStates[path_index];
	for (uint path_length = 2; path_length < gMaxPathVertices; path_length++) {

	#endif
		// trace bounce ray (sampled at previous vertex)

		DECLARE_ISECT
		float T_dir_pdf = 1;
		float T_nee_pdf = 1;
		trace_ray(gRNGStates[path_index], _position, _dir_out, _medium, _beta, T_dir_pdf, T_nee_pdf, PASS_ISECT_ARG);
		if (T_dir_pdf <= 0 || all(_beta <= 0)) TERMINATE_PATH
		_beta /= T_dir_pdf;

		// handle miss
		if (_isect.instance_index() == INVALID_INSTANCE) {
			if (gHasEnvironment) {
				Environment env;
				env.load(gEnvironmentMaterialAddress);
				gRadiance[pixel_coord.xy].rgb += _beta * env.eval(_dir_out);
			}
			TERMINATE_PATH
		}

		const float dist = length(_position - _isect.sd.position);

		// compute ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential = gRayDifferentials[path_index];
			ray_differential.transfer(dist);
			_isect.sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[path_index] = ray_differential;
		}

		// russian roullette, random numbers for material sampling while we're at it
		#ifdef BDPT_SINGLE_BOUNCE_KERNEL
		uint4 _rng = gRNGStates[path_index];
		#endif
		const float3 material_sample_rnd = float3(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng));
		if (path_length >= gMinPathVertices) {
			const float p = min(luminance(_beta), 0.95);
			if (rng_next_float(_rng) > p)
				TERMINATE_PATH
			else
				_beta /= p;
		}

		// store rng for next iteration
		#ifdef BDPT_SINGLE_BOUNCE_KERNEL
		gRNGStates[path_index] = _rng;
		#endif

		// sample next direction
		// note that _dir_out is incoming direction, since we traced the dir_out ray to get here

		DECLARE_MATERIAL_SAMPLE
		if (gHasMedia && _isect.sd.shape_area == 0) {
			if (path_length+1 > gMaxPathVertices) return;
			Medium m;
			m.load(gInstances[_medium].material_address());
			m.sample(_material_sample, material_sample_rnd, -_dir_out);
		} else {
			DECLARE_MATERIAL
			_material.load_and_sample(gInstances[_isect.instance_index()].material_address(), _isect.sd.uv, _isect.sd.uv_screen_size);

			// add emission from surface
			if (any(_material.emission > 0)) {
				gRadiance[pixel_coord.xy].rgb += _beta * _material.emission;
			}

			if (path_length+1 > gMaxPathVertices) return;

			_material.sample(_material_sample, material_sample_rnd, _isect.sd.to_local(-_dir_out), _beta, false);
			_isect.sd.position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? _isect.sd.geometry_normal() : -_isect.sd.geometry_normal());
			_material_sample.dir_out = _isect.sd.to_world(_material_sample.dir_out);
		}
		if (all(_beta <= 0)) TERMINATE_PATH

		// store vertex data for next bounce
		_position = _isect.sd.position;
		_dir_out = _material_sample.dir_out;
		#ifdef BDPT_SINGLE_BOUNCE_KERNEL
		gPathStates[path_index].beta = _beta;
		#endif

	#ifndef BDPT_SINGLE_BOUNCE_KERNEL
	}
	#endif

	#undef TERMINATE_PATH
}