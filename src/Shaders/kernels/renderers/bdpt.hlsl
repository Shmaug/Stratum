#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E visibility
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E path_step

#include <scene.h>
#include <bdpt.h>

[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;

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

[[vk::push_constant]] const BDPTPushConstants gPushConstants;
#define gViewCount 						gPushConstants.gViewCount
#define gLightCount 					gPushConstants.gLightCount
#define gEnvironmentMaterialAddress 	gPushConstants.gEnvironmentMaterialAddress
#define gEnvironmentSampleProbability 	gPushConstants.gEnvironmentSampleProbability
#define gRandomSeed 					gPushConstants.gRandomSeed
#define gMaxPathVertices 				gPushConstants.gMaxPathVertices
#define gMinPathVertices 				gPushConstants.gMinPathVertices
#define gMaxNullCollisions 				gPushConstants.gMaxNullCollisions

[[vk::binding(0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding(2,0)]] ByteAddressBuffer gIndices;
[[vk::binding(3,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(4,0)]] StructuredBuffer<TransformData> gInstanceMotionTransforms;
[[vk::binding(5,0)]] StructuredBuffer<TransformData> gInstanceInverseTransforms;
[[vk::binding(6,0)]] StructuredBuffer<uint> gLightInstances;
[[vk::binding(7,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding(8,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(9,0)]] SamplerState gSampler;
[[vk::binding(10,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(11,0)]] Texture2D<float4> gImages[gImageCount];
[[vk::binding(12,0)]] RWStructuredBuffer<uint> gRayCount;

[[vk::binding(0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding(1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding(2,1)]] StructuredBuffer<uint> gViewMediumInstances;
[[vk::binding(3,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding(4,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(5,1)]] RWTexture2D<float4> gDebugImage;
[[vk::binding(6,1)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(7,1)]] RWStructuredBuffer<uint4> gRNGStates;
[[vk::binding(8,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(9,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;

#include "../common/rng.hlsli"
typedef uint4 rng_state_t;
uint rng_next_uint(inout rng_state_t state) { return pcg_next_uint(state); }
float rng_next_float(inout rng_state_t state) { return pcg_next_float(state); }
#include "../common/intersection.hlsli"
#include <materials/environment.h>

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4

//#define GROUPSHARED_BETA
//#define GROUPSHARED_ISECT
//#define GROUPSHARED_MATERIAL
//#define GROUPSHARED_MATERIAL_SAMPLE

#ifdef GROUPSHARED_BETA
#define _beta s_beta[group_thread_index]
// 3 floats
groupshared Spectrum s_beta[GROUPSIZE_X*GROUPSIZE_Y];
#endif

#ifdef GROUPSHARED_ISECT
#define _isect s_isect[group_thread_index]
// 12 floats
groupshared IntersectionVertex s_isect[GROUPSIZE_X*GROUPSIZE_Y];
#endif

#ifdef GROUPSHARED_MATERIAL
#define _material s_material[group_thread_index]
// 11 floats
groupshared Material s_material[GROUPSIZE_X*GROUPSIZE_Y];
#endif

#ifdef GROUPSHARED_MATERIAL_SAMPLE
#define _material_sample s_material_sample[group_thread_index]
// 13 floats
groupshared MaterialSampleRecord s_material_sample[GROUPSIZE_X*GROUPSIZE_Y];
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

[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void visibility(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint view_index = get_view_index(pixel_coord.xy, gViews, gViewCount);
	if (view_index == -1) return;

	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);

	const float2 uv = (pixel_coord.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_dir_out = normalize(gViews[view_index].projection.back_project(clipPos));
	const float3 dir_out = normalize(gViews[view_index].camera_to_world.transform_vector(local_dir_out));

	gPathStates[path_index].medium = gViewMediumInstances[view_index];
	gRNGStates[path_index] = uint4(pixel_coord.xy, gRandomSeed, 0);

	const float3 origin = gViews[view_index].camera_to_world.transform_point(0);

	#ifndef GROUPSHARED_ISECT
	IntersectionVertex _isect;
	#endif
	TransmittanceEstimate T;
	intersect_scene(gRNGStates[path_index], origin, dir_out, gPathStates[path_index].medium, T, _isect);
	#ifndef GROUPSHARED_BETA
	float3 _beta;
	#endif
	_beta = T.transmittance/T.dir_pdf;

	gVisibility[path_index].position = _isect.sd.position;
	gVisibility[path_index].instance_primitive_index = _isect.instance_index | (_isect.primitive_index<<16);
	gVisibility[path_index].packed_normal = _isect.sd.packed_shading_normal;

	if (_isect.instance_index != INVALID_INSTANCE) {
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
			const InstanceData instance = gInstances[_isect.instance_index];
			material_address = instance.material_address();

			switch (instance.type()) {
			case INSTANCE_TYPE_TRIANGLES: {
				// TODO: figure out dz
				const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(_isect.sd.geometry_normal());
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
		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, gInstanceMotionTransforms[_isect.instance_index]).transform_point(_isect.sd.position);
		gVisibility[path_index].packed_z = pack_f16_2(float2(z, length(prevCamPos)));
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevScreenPos.xyz /= prevScreenPos.w;
		gVisibility[path_index].prev_uv = prevScreenPos.xy*.5 + .5;

		if (gMaxPathVertices > 1) {
			#ifndef GROUPSHARED_MATERIAL
			Material _material;
			#endif
			_material.load_and_sample(material_address, _isect.sd.uv, _isect.sd.uv_screen_size);

			gRadiance[pixel_coord.xy] = float4(_beta * _material.emission / (gDemodulateAlbedo ? _material.color : 1), 1);
			gAlbedo[pixel_coord.xy] = float4(_material.color, 1);
			if ((DebugMode)gDebugMode == DebugMode::eAlbedo)
				gDebugImage[pixel_coord.xy] = float4(_beta * _material.color, 1);

			if (gMaxPathVertices > 2) {
				// sample bsdf
				const float3 rnd = float3(rng_next_float(gRNGStates[path_index]), rng_next_float(gRNGStates[path_index]), rng_next_float(gRNGStates[path_index]));
				#ifndef GROUPSHARED_MATERIAL_SAMPLE
				MaterialSampleRecord _material_sample;
				#endif
				_material.sample(_material_sample, rnd, _isect.sd.to_local(-dir_out), false);
				if ((DebugMode)gDebugMode == DebugMode::eDirOutPdf) gDebugImage[pixel_coord.xy] = float4(_material_sample.eval.pdf_fwd.xxx, 1);
				if (_material_sample.eval.pdf_fwd > 0) {
					gPathStates[path_index].beta = _beta * _material_sample.eval.f_estimate;
					gPathStates[path_index].position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? _isect.sd.geometry_normal() : -_isect.sd.geometry_normal());
					gPathStates[path_index].dir_out = _isect.sd.to_world(_material_sample.dir_out);
					gPathStates[path_index].packed_pixel = (pixel_coord.x&0xFFFF) | pixel_coord.y<<16;
					if ((DebugMode)gDebugMode == DebugMode::eDirOut) gDebugImage[pixel_coord.xy] = float4(gPathStates[path_index].dir_out*.5+.5, 1);
				} else
					gPathStates[path_index].beta = 0;
				gPathStates[path_index].path_length = 2;
			} else
				gPathStates[path_index].path_length = 1;
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
		gVisibility[path_index].packed_z = pack_f16_2(1.#INF);
		gVisibility[path_index].packed_dz = pack_f16_2(0);
	}
}

[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void path_step(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	const uint path_length = gPathStates[path_index].path_length + 1;
	if (path_length > gMaxPathVertices) return;

	#ifndef GROUPSHARED_BETA
	float3 _beta;
	#endif
	_beta = gPathStates[path_index].beta;
	if (all(_beta <= 0)) return;

	#ifndef GROUPSHARED_ISECT
	IntersectionVertex _isect;
	#endif
	TransmittanceEstimate T;
	intersect_scene(gRNGStates[path_index], gPathStates[path_index].position, gPathStates[path_index].dir_out, gPathStates[path_index].medium, T, _isect);
	if (T.dir_pdf <= 0 || all(T.transmittance <= 0)) {
		gPathStates[path_index].beta = 0;
		return;
	}
	const float dist = length(gPathStates[path_index].position - _isect.sd.position);
	_beta *= T.transmittance / T.dir_pdf;

	if (_isect.instance_index == INVALID_INSTANCE) {
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			uint2 p;
			p.x = gPathStates[path_index].packed_pixel&0xFFFF;
			p.y = gPathStates[path_index].packed_pixel>>16;
			gRadiance[pixel_coord.xy].rgb += _beta * env.eval(gPathStates[path_index].dir_out);
		}
		gPathStates[path_index].beta = 0;
		return;
	}

	if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
		RayDifferential ray_differential = gRayDifferentials[path_index];
		ray_differential.transfer(dist);
		_isect.sd.uv_screen_size *= ray_differential.radius;
		gRayDifferentials[path_index] = ray_differential;
	}

	uint4 rng = gRNGStates[path_index];
	const float3 material_sample_rnd = float3(rng_next_float(rng), rng_next_float(rng), rng_next_float(rng));
	if (path_length >= gMinPathVertices) {
		const float p = min(luminance(_beta), 0.95);
		if (rng_next_float(rng) > p) {
			gPathStates[path_index].beta = 0;
			return;
		} else
			_beta /= p;
	}
	gRNGStates[path_index] = rng;

	#ifndef GROUPSHARED_MATERIAL
	Material _material;
	#endif
	_material.load_and_sample(gInstances[_isect.instance_index].material_address(), _isect.sd.uv, _isect.sd.uv_screen_size);

	if (any(_material.emission > 0)) {
		uint2 p;
		p.x = gPathStates[path_index].packed_pixel&0xFFFF;
		p.y = gPathStates[path_index].packed_pixel>>16;
		gRadiance[pixel_coord.xy].rgb += _beta * _material.emission;
	}

	#ifndef GROUPSHARED_MATERIAL_SAMPLE
	MaterialSampleRecord _material_sample;
	#endif
	_material.sample(_material_sample, material_sample_rnd, _isect.sd.to_local(-gPathStates[path_index].dir_out), false);
	if (_material_sample.eval.pdf_fwd > 0 && any(_material_sample.eval.f_estimate > 0)) {
		gPathStates[path_index].position = ray_offset(_isect.sd.position, _material_sample.dir_out.z > 0 ? _isect.sd.geometry_normal() : -_isect.sd.geometry_normal());
		gPathStates[path_index].beta = _beta * _material_sample.eval.f_estimate;
		gPathStates[path_index].dir_out = _isect.sd.to_world(_material_sample.dir_out);
		gPathStates[path_index].path_length = path_length;
	} else {
		gPathStates[path_index].beta = 0;
	}
}