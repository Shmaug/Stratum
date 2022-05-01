#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E visibility
#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E path_step

#include <scene.h>
#include <bdpt.h>

[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;

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

groupshared Spectrum s_beta[8*4];
groupshared IntersectionVertex s_isect[8*4];
groupshared MaterialSampleRecord s_material_samples[8*4];

[numthreads(8,4,1)]
void visibility(uint3 index : SV_DispatchThreadID, uint group_idx : SV_GroupIndex) {
	const uint view_index = get_view_index(index.xy, gViews, gViewCount);
	if (view_index == -1) return;

	uint w,h;
	gRadiance.GetDimensions(w,h);
	const uint index_1d = index.y*w + index.x;

	const float2 uv = (index.xy + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_dir_out = normalize(gViews[view_index].projection.back_project(clipPos));
	const float3 dir_out = normalize(gViews[view_index].camera_to_world.transform_vector(local_dir_out));

	const float3 origin = gViews[view_index].camera_to_world.transform_point(0);

	RayQuery<RAY_FLAG_NONE> rayQuery;
	gPathStates[index_1d].medium = gViewMediumInstances[view_index];
	gRNGStates[index_1d] = uint4(index.xy, gRandomSeed, 0);
	TransmittanceEstimate T;
	intersect_scene(gRNGStates[index_1d], origin, dir_out, gPathStates[index_1d].medium, T, s_isect[group_idx]);
	s_beta[group_idx] = T.transmittance/T.dir_pdf;

	gVisibility[index_1d].position = s_isect[group_idx].sd.position;
	gVisibility[index_1d].instance_primitive_index = s_isect[group_idx].instance_index | (s_isect[group_idx].primitive_index<<16);
	gVisibility[index_1d].packed_normal = s_isect[group_idx].sd.packed_shading_normal;

	if (s_isect[group_idx].instance_index != INVALID_INSTANCE) {
		if ((DebugMode)gDebugMode == DebugMode::eGeometryNormal)     gDebugImage[index.xy] = float4(s_isect[group_idx].sd.geometry_normal()*.5+.5, 1);
		else if ((DebugMode)gDebugMode == DebugMode::eShadingNormal) gDebugImage[index.xy] = float4(s_isect[group_idx].sd.shading_normal()*.5+.5, 1);

		const float z = length(origin - s_isect[group_idx].sd.position);
		// update ray differentials
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
			RayDifferential ray_differential;
			ray_differential.radius = 0;
			ray_differential.spread = 1 / min(gViews[view_index].extent().x, gViews[view_index].extent().y);
			ray_differential.transfer(z);
			s_isect[group_idx].sd.uv_screen_size *= ray_differential.radius;
			gRayDifferentials[index_1d] = ray_differential;
		}
		uint material_address;
		{
			const InstanceData instance = gInstances[s_isect[group_idx].instance_index];
			material_address = instance.material_address();

			switch (instance.type()) {
			case INSTANCE_TYPE_TRIANGLES: {
				// TODO: figure out dz
				const float3 view_normal = gViews[view_index].world_to_camera.transform_vector(s_isect[group_idx].sd.geometry_normal());
				gVisibility[index_1d].packed_dz = pack_f16_2(1/(abs(view_normal.xy) + 1e-2));
				break;
			}
			case INSTANCE_TYPE_SPHERE:
				gVisibility[index_1d].packed_dz = pack_f16_2(1/sqrt(instance.radius()));
				break;
			case INSTANCE_TYPE_VOLUME:
				gVisibility[index_1d].packed_dz = pack_f16_2(1);
				break;
			}
		}
		const float3 prevCamPos = tmul(gPrevViews[view_index].world_to_camera, gInstanceMotionTransforms[s_isect[group_idx].instance_index]).transform_point(s_isect[group_idx].sd.position);
		gVisibility[index_1d].packed_z = pack_f16_2(float2(z, length(prevCamPos)));
		float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevScreenPos.xyz /= prevScreenPos.w;
		gVisibility[index_1d].prev_uv = prevScreenPos.xy*.5 + .5;

		if (gMaxPathVertices > 1) {
			Material material;
			material.load_and_sample(material_address, s_isect[group_idx].sd.uv, s_isect[group_idx].sd.uv_screen_size);

			gRadiance[index.xy] = float4(s_beta[group_idx] * material.emission / (gDemodulateAlbedo ? material.color : 1), 1);
			gAlbedo[index.xy] = float4(material.color, 1);
			if ((DebugMode)gDebugMode == DebugMode::eAlbedo)
				gDebugImage[index.xy] = float4(s_beta[group_idx] * material.color, 1);

			if (gMaxPathVertices > 2) {
				// sample bsdf
				MaterialSampleRecord r;
				const float3 rnd = float3(rng_next_float(gRNGStates[index_1d]), rng_next_float(gRNGStates[index_1d]), rng_next_float(gRNGStates[index_1d]));
				material.sample(r, rnd, s_isect[group_idx].sd.to_local(-dir_out), false);
				if ((DebugMode)gDebugMode == DebugMode::eDirOutPdf) gDebugImage[index.xy] = float4(r.eval.pdf_fwd.xxx, 1);
				if (r.eval.pdf_fwd > 0) {
					gPathStates[index_1d].beta = s_beta[group_idx] * r.eval.f_estimate;
					gPathStates[index_1d].position = ray_offset(s_isect[group_idx].sd.position, r.dir_out.z > 0 ? s_isect[group_idx].sd.geometry_normal() : -s_isect[group_idx].sd.geometry_normal());
					gPathStates[index_1d].dir_out = s_isect[group_idx].sd.to_world(r.dir_out);
					gPathStates[index_1d].packed_pixel = (index.x&0xFFFF) | index.y<<16;
					if ((DebugMode)gDebugMode == DebugMode::eDirOut) gDebugImage[index.xy] = float4(gPathStates[index_1d].dir_out*.5+.5, 1);
				} else
					gPathStates[index_1d].beta = 0;
				gPathStates[index_1d].path_length = 2;
			} else
				gPathStates[index_1d].path_length = 1;
		}
	} else {
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			gRadiance[index.xy] = float4(s_beta[group_idx] * env.eval(dir_out), 1);
			gAlbedo[index.xy] = 1;
		} else {
			gRadiance[index.xy] = float4(0,0,0,1);
			gAlbedo[index.xy] = float4(0,0,0,1);
		}
		gPathStates[index_1d].beta = 0;
		gVisibility[index_1d].prev_uv = uv;
		gVisibility[index_1d].packed_z = pack_f16_2(1.#INF);
		gVisibility[index_1d].packed_dz = pack_f16_2(0);
	}
}

[numthreads(8,4,1)]
void path_step(uint3 index : SV_DispatchThreadID, uint group_idx : SV_GroupIndex) {
	uint2 extent;
	gRadiance.GetDimensions(extent.x, extent.y);
	if (any(index.xy >= extent)) return;

	const uint index_1d = index.y*extent.x + index.x;

	if (gPathStates[index_1d].path_length >= gMaxPathVertices) return;
	gPathStates[index_1d].path_length++;
	s_beta[group_idx] = gPathStates[index_1d].beta;

	if (all(s_beta[group_idx] <= 0)) return;

	TransmittanceEstimate T;
	intersect_scene(gRNGStates[index_1d], gPathStates[index_1d].position, gPathStates[index_1d].dir_out, gPathStates[index_1d].medium, T, s_isect[group_idx]);
	if (T.dir_pdf <= 0 || all(T.transmittance <= 0)) {
		gPathStates[index_1d].beta = 0;
		return;
	}
	s_beta[group_idx] *= T.transmittance / T.dir_pdf;

	if (s_isect[group_idx].instance_index == INVALID_INSTANCE) {
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			uint2 p;
			p.x = gPathStates[index_1d].packed_pixel&0xFFFF;
			p.y = gPathStates[index_1d].packed_pixel>>16;
			gRadiance[p].rgb += s_beta[group_idx] * env.eval(gPathStates[index_1d].dir_out);
		}
		gPathStates[index_1d].beta = 0;
		return;
	}

	if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
		RayDifferential ray_differential = gRayDifferentials[index_1d];
		ray_differential.transfer(length(gPathStates[index_1d].position - s_isect[group_idx].sd.position));
		s_isect[group_idx].sd.uv_screen_size *= ray_differential.radius;
		gRayDifferentials[index_1d] = ray_differential;
	}

	if (gPathStates[index_1d].path_length+1 > gMinPathVertices) {
		const float p = min(luminance(s_beta[group_idx]), 0.95);
		if (rng_next_float(gRNGStates[index_1d]) > p) {
			gPathStates[index_1d].beta = 0;
			return;
		} else
			s_beta[group_idx] /= p;
	}

	uint4 rng = gRNGStates[index_1d];
	const float3 rnd = float3(rng_next_float(rng), rng_next_float(rng), rng_next_float(rng));
	gRNGStates[index_1d] = rng;

	Material material;
	material.load_and_sample(gInstances[s_isect[group_idx].instance_index].material_address(), s_isect[group_idx].sd.uv, s_isect[group_idx].sd.uv_screen_size);

	if (any(material.emission > 0)) {
		uint2 p;
		p.x = gPathStates[index_1d].packed_pixel&0xFFFF;
		p.y = gPathStates[index_1d].packed_pixel>>16;
		gRadiance[p].rgb += s_beta[group_idx] * material.emission;
	}

	MaterialSampleRecord r;
	material.sample(r, rnd, s_isect[group_idx].sd.to_local(-gPathStates[index_1d].dir_out), false);
	if (r.eval.pdf_fwd > 0 && any(r.eval.f_estimate > 0)) {
		gPathStates[index_1d].position = ray_offset(s_isect[group_idx].sd.position, r.dir_out.z > 0 ? s_isect[group_idx].sd.geometry_normal() : -s_isect[group_idx].sd.geometry_normal());
		gPathStates[index_1d].beta = s_beta[group_idx] * r.eval.f_estimate;
		gPathStates[index_1d].dir_out = s_isect[group_idx].sd.to_world(r.dir_out);
	} else {
		gPathStates[index_1d].beta = 0;
	}
}