#if 0
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_visibility
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_photons
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E path_trace_loop
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_nee
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -T cs_6_7 -HV 2021 -E presample_lights
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry path_trace_loop
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry trace_nee
#pragma compile slangc -profile sm_6_6 -lang slang -entry presample_lights
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
#ifndef gKernelIterationCount
#define gKernelIterationCount 0
#endif
#ifndef gLightTraceQuantization
#define gLightTraceQuantization 16384
#endif
[[vk::push_constant]] ConstantBuffer<BDPTPushConstants> gPushConstants;
#else // !defined(__SLANG__)
[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::constant_id(2)]] const uint gKernelIterationCount = 0;
[[vk::constant_id(3)]] const uint gLightTraceQuantization = 65536;
[[vk::push_constant]] const BDPTPushConstants gPushConstants;
#endif

#define gHasEnvironment         (gSpecializationFlags & BDPT_FLAG_HAS_ENVIRONMENT)
#define gHasEmissives           (gSpecializationFlags & BDPT_FLAG_HAS_EMISSIVES)
#define gHasMedia               (gSpecializationFlags & BDPT_FLAG_HAS_MEDIA)
#define gRemapThreadIndex       (gSpecializationFlags & BDPT_FLAG_REMAP_THREADS)
#define gCoherentRR             (gSpecializationFlags & BDPT_FLAG_COHERENT_RR)
#define gCoherentRNG            (gSpecializationFlags & BDPT_FLAG_COHERENT_RNG)
#define gUseRayCones            (gSpecializationFlags & BDPT_FLAG_RAY_CONES)
#define gSampleBSDFs            (gSpecializationFlags & BDPT_FLAG_SAMPLE_BSDFS)
#define gSampleLightPower       (gSpecializationFlags & BDPT_FLAG_SAMPLE_LIGHT_POWER)
#define gUniformSphereSampling  (gSpecializationFlags & BDPT_FLAG_UNIFORM_SPHERE_SAMPLING)
#define gUseMIS                 (gSpecializationFlags & BDPT_FLAG_MIS)
#define gUseNEE                 (gSpecializationFlags & BDPT_FLAG_NEE)
#define gPresampleLights        (gSpecializationFlags & BDPT_FLAG_PRESAMPLE_LIGHTS)
#define gReservoirNEE           (gSpecializationFlags & BDPT_FLAG_RESERVOIR_NEE)
#define gReservoirTemporalReuse (gSpecializationFlags & BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE)
#define gReservoirSpatialReuse  (gSpecializationFlags & BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)
#define gReservoirUnbiasedReuse (gSpecializationFlags & BDPT_FLAG_RESERVOIR_UNBIASED_REUSE)
#define gDeferNEERays           (gSpecializationFlags & BDPT_FLAG_DEFER_NEE_RAYS)
#define gTraceLight             (gSpecializationFlags & BDPT_FLAG_TRACE_LIGHT)
#define gConnectToViews         (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_VIEWS)
#define gConnectToLightPaths    (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)
#define gCountRays              (gSpecializationFlags & BDPT_FLAG_COUNT_RAYS)

#define gOutputExtent				  gPushConstants.gOutputExtent
#define gViewCount 					  gPushConstants.gViewCount
#define gLightCount 				  gPushConstants.gLightCount
#define gLightDistributionPDF		  gPushConstants.gLightDistributionPDF
#define gLightDistributionCDF		  gPushConstants.gLightDistributionCDF
#define gEnvironmentMaterialAddress   gPushConstants.gEnvironmentMaterialAddress
#define gEnvironmentSampleProbability gPushConstants.gEnvironmentSampleProbability
#define gRandomSeed 				  gPushConstants.gRandomSeed
#define gMinPathVertices 			  gPushConstants.gMinPathVertices
#define gMaxPathVertices 			  gPushConstants.gMaxPathVertices
#define gMaxLightPathVertices 		  gPushConstants.gMaxLightPathVertices
#define gMaxNullCollisions 			  gPushConstants.gMaxNullCollisions
#define gLightPathCount 			  gPushConstants.gLightPathCount
#define gNEEReservoirM 				  gPushConstants.gNEEReservoirM
#define gNEEReservoirSpatialSamples   gPushConstants.gNEEReservoirSpatialSamples
#define gNEEReservoirSpatialRadius 	  gPushConstants.gNEEReservoirSpatialRadius
#define gReservoirMaxM 				  gPushConstants.gReservoirMaxM
#define gLightPresampleTileSize		  gPushConstants.gLightPresampleTileSize
#define gLightPresampleTileCount	  gPushConstants.gLightPresampleTileCount

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
[[vk::binding(13,0)]] Texture2D<float4> gImages[gImageCount];

[[vk::binding( 0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding( 1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding( 2,1)]] StructuredBuffer<TransformData> gViewTransforms;
[[vk::binding( 3,1)]] StructuredBuffer<TransformData> gInverseViewTransforms;
[[vk::binding( 4,1)]] StructuredBuffer<TransformData> gPrevInverseViewTransforms;
[[vk::binding( 5,1)]] StructuredBuffer<uint> gViewMediumInstances;
[[vk::binding( 6,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding( 7,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding( 8,1)]] RWTexture2D<float4> gDebugImage;
[[vk::binding( 9,1)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(10,1)]] RWTexture2D<float2> gPrevUVs;
[[vk::binding(11,1)]] StructuredBuffer<VisibilityInfo> gPrevVisibility;
[[vk::binding(12,1)]] RWStructuredBuffer<PathState> gPathStates;
[[vk::binding(13,1)]] RWStructuredBuffer<PathState1> gPathStates1;
[[vk::binding(14,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;
[[vk::binding(15,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(16,1)]] RWStructuredBuffer<uint4> gReservoirSamples;
[[vk::binding(17,1)]] RWStructuredBuffer<Reservoir> gPrevReservoirs;
[[vk::binding(18,1)]] RWStructuredBuffer<uint4> gPrevReservoirSamples;
[[vk::binding(19,1)]] RWStructuredBuffer<PresampledLightPoint> gPresampledLights;
[[vk::binding(20,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(21,1)]] RWStructuredBuffer<LightPathVertex> gLightPathVertices;
[[vk::binding(22,1)]] RWStructuredBuffer<LightPathVertex1> gLightPathVertices1;
[[vk::binding(23,1)]] RWStructuredBuffer<NEERayData> gNEERays;

#include "../../common/path.hlsli"

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4
uint map_pixel_coord(const uint2 pixel_coord, const uint2 group_id, const uint group_thread_index) {
	uint path_index;
	if (gRemapThreadIndex) {
		const uint dispatch_w = (gOutputExtent.x + GROUPSIZE_X - 1) / GROUPSIZE_X;
		const uint group_index = group_id.y*dispatch_w + group_id.x;
		path_index = group_index*GROUPSIZE_X*GROUPSIZE_Y + group_thread_index;
	} else
		path_index = pixel_coord.y*gOutputExtent.x + pixel_coord.x;

	return path_index < gOutputExtent.x*gOutputExtent.y ? path_index : -1;
}

SLANG_SHADER("compute")
[numthreads(64,1,1)]
void presample_lights(uint3 index : SV_DispatchThreadID) {
	if (index.x >= gLightPresampleTileSize*gLightPresampleTileCount) return;

	rng_state_t _rng = rng_init(-1, index.x);
	LightSampleRecord ls;
	sample_point_on_light(ls, float4(rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng), rng_next_float(_rng)), 0);

	PresampledLightPoint l;
	l.position = ls.position;
	l.packed_geometry_normal = pack_normal_octahedron(ls.normal);
	l.Le = ls.radiance;
	l.pdfA = ls.is_environment() ? -ls.pdf : ls.pdf;
	gPresampledLights[index.x] = l;
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_photons(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	PathIntegrator path;
	path.path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path.path_index == -1) return;
	path.pixel_coord = index.xy;
	path.path_length = 1;

	path.init_rng();

	// sample point on light

	LightSampleRecord ls;
	sample_point_on_light(ls, float4(rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng)), 0);
	if (ls.pdf <= 0 || all(ls.radiance <= 0)) { gPathStates[path.path_index].beta = 0; return; }

	path._medium = -1;
	path._isect.sd.position = ls.position;
	path._isect.sd.packed_geometry_normal = pack_normal_octahedron(ls.normal);
	path._isect.sd.shape_area = 1; // just needs to be >0 so that its not treated as a volume sample

	path._beta = ls.radiance / ls.pdf;
	path.d = N_kk / ls.pdf;
	path.G = 1;
	path.prev_cos_out = 1;
	path.bsdf_pdf = ls.pdf;

	if (gConnectToLightPaths) path.store_light_vertex();

	if (gMaxLightPathVertices < 2) return;

	// sample direction
	const Vector3 local_dir_out = sample_cos_hemisphere(rng_next_float(path._rng), rng_next_float(path._rng));
	path.bsdf_pdf = cosine_hemisphere_pdfW(local_dir_out.z);
	path._beta /= path.bsdf_pdf;

	// cosine term from light surface
	path._beta *= local_dir_out.z;
	path.prev_cos_out = local_dir_out.z;

	Vector3 T,B;
	make_orthonormal(ls.normal, T, B);
	path.direction = T*local_dir_out.x + B*local_dir_out.y + ls.normal*local_dir_out.z;
	path.origin = ray_offset(ls.position, ls.normal);

	path.trace();

	if (gKernelIterationCount == 0) {
		while (path.path_length <= PathIntegrator::gMaxVertices && any(path._beta > 0) && !any(isnan(path._beta)))
			path.next_vertex();
	} else {
		if (path._isect.instance_index() == INVALID_INSTANCE)
			gPathStates[path.path_index].beta = 0;
		else
			path.store_state();
	}
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_visibility(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	PathIntegrator path;
	path.path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path.path_index == -1) return;
	path.pixel_coord = index.xy;

	const uint view_index = get_view_index(index.xy, gViews, gViewCount);
	if (view_index == -1) return;

	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution || (BDPTDebugMode)gDebugMode == BDPTDebugMode::eViewTraceContribution)
		gDebugImage[path.pixel_coord] = float4(0,0,0,1);

	// initialize radiance with light trace sample
	Spectrum c = 0;
	if (gConnectToViews && (BDPTDebugMode)gDebugMode != BDPTDebugMode::eViewTraceContribution) {
		c = ViewConnection::load_sample(path.pixel_coord);
		if (any(c < 0) || any(isnan(c))) c = 0;
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eLightTraceContribution || ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1))
			gDebugImage[path.pixel_coord] = float4(c, 1);
	}
	gRadiance[path.pixel_coord] = float4(c,1);

	if (gMaxPathVertices < 2) return;

	// initialize ray differential
	if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
		RayDifferential ray_differential;
		ray_differential.radius = 0;
		ray_differential.spread = 1 / min(gViews[view_index].extent().x, gViews[view_index].extent().y);
		gRayDifferentials[path.path_index] = ray_differential;
	}

	// initialize ray
	const float2 uv = (path.pixel_coord + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clip_pos = 2*uv - 1;
	clip_pos.y = -clip_pos.y;
	const Vector3 local_dir_out = normalize(gViews[view_index].projection.back_project(clip_pos));
	path.prev_cos_out = abs(local_dir_out.z);
	path.direction = normalize(gViewTransforms[view_index].transform_vector(local_dir_out));

	path.origin = Vector3(
		gViewTransforms[view_index].m[0][3],
		gViewTransforms[view_index].m[1][3],
		gViewTransforms[view_index].m[2][3] );
	path._medium = gViewMediumInstances[view_index];

	path.path_length = 1;
	path._beta = 1;
	path.bsdf_pdf = 1 / (gViews[view_index].projection.sensor_area * pow3(path.prev_cos_out));

	path.init_rng();

	// trace visibility ray
	path.trace();

	path.d = N_sk(0) / 1;

	if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eGeometryNormal) gDebugImage[path.pixel_coord] = float4(path._isect.sd.geometry_normal()*.5+.5, 1);
	else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eShadingNormal)  gDebugImage[path.pixel_coord] = float4(path._isect.sd.shading_normal() *.5+.5, 1);

	// store visibility
	VisibilityInfo vis;
	vis.instance_primitive_index = path._isect.instance_primitive_index;
	vis.packed_normal = path._isect.sd.packed_shading_normal;

	// handle miss
	if (path._isect.instance_index() == INVALID_INSTANCE) {
		gAlbedo[path.pixel_coord] = 1;
		vis.packed_z = pack_f16_2(POS_INFINITY);
		gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;
		gPrevUVs[path.pixel_coord.xy] = uv;
		gPathStates[path.path_index].beta = 0;
		if (gUseNEE && gReservoirNEE) gReservoirs[path.path_index].init();
		Material _material;
		path.eval_emission(_material);
		return;
	}

	const Real z = length(path._isect.sd.position - path.origin);

	float2 dz_dxy;
	{
		const float2 uv_x = (path.pixel_coord + uint2(1,0) + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
		float2 clipPos_x = 2*uv_x - 1;
		clipPos_x.y = -clipPos_x.y;
		const Vector3 dir_out_x = normalize(gViewTransforms[view_index].transform_vector(normalize(gViews[view_index].projection.back_project(clipPos_x))));
		dz_dxy.x = ray_plane(path.origin - path._isect.sd.position, dir_out_x, path._isect.sd.geometry_normal()) - z;
	}
	{
		const float2 uv_y = (path.pixel_coord + uint2(0,1) + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
		float2 clipPos_y = 2*uv_y - 1;
		clipPos_y.y = -clipPos_y.y;
		const Vector3 dir_out_y = normalize(gViewTransforms[view_index].transform_vector(normalize(gViews[view_index].projection.back_project(clipPos_y))));
		dz_dxy.y = ray_plane(path.origin - path._isect.sd.position, dir_out_y, path._isect.sd.geometry_normal()) - z;
	}

	//const Vector3 prev_cam_pos = tmul(gPrevInverseViewTransforms[view_index], gInstanceMotionTransforms[path._isect.instance_index()]).transform_point(path._isect.sd.position);
	const Vector3 prev_cam_pos = gPrevInverseViewTransforms[view_index].transform_point(path._isect.sd.position);
	vis.packed_z = pack_f16_2(float2(z, length(prev_cam_pos)));
	vis.packed_dz = pack_f16_2(dz_dxy);
	gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;

	// calculate prev uv
	float4 prev_clip_pos = gPrevViews[view_index].projection.project_point(prev_cam_pos);
	prev_clip_pos.y = -prev_clip_pos.y;
	prev_clip_pos.xyz /= prev_clip_pos.w;
	gPrevUVs[path.pixel_coord.xy] = prev_clip_pos.xy*.5 + .5;
	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePrevUV)
		gDebugImage[path.pixel_coord] = float4(abs(gPrevUVs[path.pixel_coord.xy] - uv)*gOutputExtent, 0, 1);

	// evaluate albedo and emission
	if (!gHasMedia || path._isect.sd.shape_area > 0) {
		const uint material_address = gInstances[path._isect.instance_index()].material_address();
		Material _material;
		_material.load(material_address, path._isect.sd.uv, path._isect.sd.uv_screen_size);

		path.eval_emission(_material);

		const Spectrum albedo = _material.diffuse_reflectance + 0.25*_material.specular_reflectance;
		gAlbedo[path.pixel_coord] = float4(albedo, 1);

		if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eAlbedo) 		  gDebugImage[path.pixel_coord] = float4(albedo, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eDiffuse) 		  gDebugImage[path.pixel_coord] = float4(_material.diffuse_reflectance, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eSpecular) 	  gDebugImage[path.pixel_coord] = float4(_material.specular_reflectance, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eTransmission)   gDebugImage[path.pixel_coord] = float4(_material.specular_transmittance.xxx, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eRoughness) 	  gDebugImage[path.pixel_coord] = float4(sqrt(_material.alpha).xxx, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEmission) 	  gDebugImage[path.pixel_coord] = float4(_material.emission, 1);
	}

	if (gKernelIterationCount == 0) {
		while (path.path_length <= PathIntegrator::gMaxVertices && any(path._beta > 0) && !any(isnan(path._beta)))
			path.next_vertex();
	} else
		path.store_state();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void path_trace_loop(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	PathIntegrator path;
	path.load_state(path_index, index.xy);

	for (uint i = 0; i < gKernelIterationCount; i++)
		if (any(path._beta > 0) && !any(isnan(path._beta)))
			path.next_vertex();

	if (any(path._beta > 0) && !any(isnan(path._beta)))
		path.store_state();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void trace_nee(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	const uint2 pixel_coord = index.xy;

	Spectrum c = 0;
	for (int path_length = 2; path_length < gMaxPathVertices; path_length++) {
		const NEERayData rd = gNEERays[PathIntegrator::nee_vertex_index(path_index, path_length)];
		if (all(rd.contribution <= 0)) continue;

		rng_state_t _rng = rng_init(pixel_coord, rd.rng_offset);

		Spectrum beta = 1;
		Real T_dir_pdf = 1;
		Real T_nee_pdf = 1;
		trace_visibility_ray(_rng, rd.ray_origin, rd.ray_direction, rd.dist, rd.medium, beta, T_dir_pdf, T_nee_pdf);
		if (T_nee_pdf > 0) beta /= T_nee_pdf;
		c += rd.contribution * beta;
	}

	gRadiance[pixel_coord] += float4(c, 0);
}