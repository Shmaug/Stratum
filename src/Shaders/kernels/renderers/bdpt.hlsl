#if 0
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_visibility
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_photons
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E multi_kernel
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E single_kernel
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -T cs_6_7 -HV 2021 -E presample_lights
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry multi_kernel
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry single_kernel
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
[[vk::push_constant]] ConstantBuffer<BDPTPushConstants> gPushConstants;
#else // __SLANG__
#define SLANG_SHADER("compute")
[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::push_constant]] const BDPTPushConstants gPushConstants;
#endif

static const bool gHasEnvironment		 	= (gSpecializationFlags & BDPT_FLAG_HAS_ENVIRONMENT);
static const bool gHasEmissives 		 	= (gSpecializationFlags & BDPT_FLAG_HAS_EMISSIVES);
static const bool gHasMedia 			 	= (gSpecializationFlags & BDPT_FLAG_HAS_MEDIA);
static const bool gRemapThreadIndex		 	= (gSpecializationFlags & BDPT_FLAG_REMAP_THREADS);
static const bool gDemodulateAlbedo		 	= (gSpecializationFlags & BDPT_FLAG_DEMODULATE_ALBEDO);
static const bool gUseRayCones 			 	= (gSpecializationFlags & BDPT_FLAG_RAY_CONES);
static const bool gSampleBSDFs	 		 	= (gSpecializationFlags & BDPT_FLAG_SAMPLE_BSDFS);
static const bool gUseNEE		 		 	= (gSpecializationFlags & BDPT_FLAG_NEE);
static const bool gUseNEEMIS 				= (gSpecializationFlags & BDPT_FLAG_NEE_MIS);
static const bool gSampleLightPower			= (gSpecializationFlags & BDPT_FLAG_SAMPLE_LIGHT_POWER);
static const bool gUniformSphereSampling 	= (gSpecializationFlags & BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
static const bool gReservoirNEE 			= (gSpecializationFlags & BDPT_FLAG_RESERVOIR_NEE);
static const bool gPresampleLights			= (gSpecializationFlags & BDPT_FLAG_PRESAMPLE_LIGHTS);
static const bool gReservoirTemporalReuse	= (gSpecializationFlags & BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE);
static const bool gReservoirSpatialReuse	= (gSpecializationFlags & BDPT_FLAG_RESERVOIR_SPATIAL_REUSE);
static const bool gReservoirUnbiasedReuse	= (gSpecializationFlags & BDPT_FLAG_RESERVOIR_UNBIASED_REUSE);
static const bool gTraceLight 				= (gSpecializationFlags & BDPT_FLAG_TRACE_LIGHT);
static const bool gConnectToViews 			= (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_VIEWS);
static const bool gConnectToLightPaths		= (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_LIGHT_PATHS);
static const bool gCountRays 			 	= (gSpecializationFlags & BDPT_FLAG_COUNT_RAYS);

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
#define gLightPathCount 				gPushConstants.gLightPathCount
#define gNEEReservoirM 					gPushConstants.gNEEReservoirM
#define gNEEReservoirSpatialSamples 	gPushConstants.gNEEReservoirSpatialSamples
#define gNEEReservoirSpatialRadius 		gPushConstants.gNEEReservoirSpatialRadius
#define gReservoirMaxM 					gPushConstants.gReservoirMaxM
#define gLightPresampleTileSize			gPushConstants.gLightPresampleTileSize
#define gLightPresampleTileCount		gPushConstants.gLightPresampleTileCount

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
[[vk::binding(13,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;
[[vk::binding(14,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(15,1)]] RWStructuredBuffer<uint4> gReservoirSamples;
[[vk::binding(16,1)]] RWStructuredBuffer<Reservoir> gPrevReservoirs;
[[vk::binding(17,1)]] RWStructuredBuffer<uint4> gPrevReservoirSamples;
[[vk::binding(18,1)]] RWStructuredBuffer<PresampledLightPoint> gPresampledLights;
[[vk::binding(19,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(20,1)]] RWStructuredBuffer<LightPathVertex0> gLightPathVertices0;
[[vk::binding(21,1)]] RWStructuredBuffer<LightPathVertex1> gLightPathVertices1;
[[vk::binding(22,1)]] RWStructuredBuffer<LightPathVertex2> gLightPathVertices2;
[[vk::binding(23,1)]] RWStructuredBuffer<LightPathVertex3> gLightPathVertices3;

#include "../common/path.hlsli"

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4
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

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_photons(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	PathIntegrator path;
	path.path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path.path_index == -1) return;
	path.pixel_coord = pixel_coord.xy;
	path.path_length = 1;

	path.init_rng();

	// sample point on light

	LightSampleRecord ls;
	sample_point_on_light(ls, float4(rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng)), 0);
	if (ls.pdf <= 0 || all(ls.radiance <= 0)) { gPathStates[path.path_index].beta = 0; return; }

	path._beta = ls.radiance/ls.pdf;
	path.bsdf_pdf = ls.pdf;
	path._medium = -1;
	path._isect.sd.position = ls.position;
	path._isect.sd.packed_geometry_normal = pack_normal_octahedron(ls.normal);

	path.store_light_vertex();

	if (gMaxLightPathVertices < 2) return;

	// sample direction

	const float3 local_dir_out = sample_cos_hemisphere(rng_next_float(path._rng), rng_next_float(path._rng));
	path._beta /= cosine_hemisphere_pdfW(local_dir_out.z);

	// cosine term from light surface
	path._beta *= local_dir_out.z;

	float3 T,B;
	make_orthonormal(ls.normal, T, B);
	path.direction = T*local_dir_out.x + B*local_dir_out.y + ls.normal*local_dir_out.z;
	path.origin = ray_offset(ls.position, ls.normal);

	path.trace();

	if (path._isect.instance_index() == INVALID_INSTANCE) { gPathStates[path.path_index].beta = 0; return; }

	path.store_light_vertex();

	if (any(path._beta > 0)) {
		PathState ps;
		ps.p.local_position = path.local_position;
		ps.p.instance_primitive_index = path._isect.instance_primitive_index;
		ps.origin = path.origin;
		ps.pack_path_length_medium(2, path._medium);
		ps.beta = path._beta;
		ps.bsdf_pdf = path.bsdf_pdf;
		ps.dir_in = path.direction;
		gPathStates[path.path_index] = ps;
	} else
		gPathStates[path.path_index].beta = 0;
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_visibility(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint view_index = get_view_index(pixel_coord.xy, gViews, gViewCount);
	if (view_index == -1) return;

	PathIntegrator path;
	path.path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path.path_index == -1) return;
	path.pixel_coord = pixel_coord.xy;
	path.path_length = 1;
	path._beta = 1;

	if ((BDPTDebugMode) gDebugMode == BDPTDebugMode::ePathLengthContribution) gDebugImage[path.pixel_coord] = float4(0,0,0,1);

	float3 c = 0;
	if (gConnectToViews) {
		c = ViewConnection::load_sample(path.pixel_coord);
		if (any(isnan(c)) || any(isinf(c)) || any(c < 0)) c = 0;
		if (((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1))
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
	float2 clipPos = 2*uv - 1;
	clipPos.y = -clipPos.y;
	const float3 local_dir_out = normalize(gViews[view_index].projection.back_project(clipPos));
	path.direction = normalize(gViewTransforms[view_index].transform_vector(local_dir_out));

	path.origin = float3(
		gViewTransforms[view_index].m[0][3],
		gViewTransforms[view_index].m[1][3],
		gViewTransforms[view_index].m[2][3] );
	path._medium = gViewMediumInstances[view_index];

	path.init_rng();

	// trace visibility ray
	path.trace();

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
		vis.packed_dz = pack_f16_2(0);
		gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;
		gPrevUVs[path.pixel_coord.xy] = uv;
		gPathStates[path.path_index].beta = 0;
		if (gUseNEE && gReservoirNEE) gReservoirs[path.path_index].init();
		Material _material;
		path.eval_emission(_material);
		return;
	}

	const float z = length(path.origin - path._isect.sd.position);

	uint material_address;
	// store visibility packed_dz
	{
		const InstanceData instance = gInstances[path._isect.instance_index()];
		material_address = instance.material_address();

		switch (instance.type()) {
		case INSTANCE_TYPE_TRIANGLES: {
			// TODO: figure out dz
			const float3 view_normal = gInverseViewTransforms[view_index].transform_vector(path._isect.sd.geometry_normal());
			vis.packed_dz = pack_f16_2(1/(abs(view_normal.xy) + 1e-2));
			break;
		}
		case INSTANCE_TYPE_SPHERE:
			vis.packed_dz = pack_f16_2(1/sqrt(instance.radius()));
			break;
		case INSTANCE_TYPE_VOLUME:
			vis.packed_dz = pack_f16_2(1);
			break;
		}
	}

	//const float3 prev_cam_pos = tmul(gPrevInverseViewTransforms[view_index], gInstanceMotionTransforms[path._isect.instance_index()]).transform_point(path._isect.sd.position);
	const float3 prev_cam_pos = gPrevInverseViewTransforms[view_index].transform_point(path._isect.sd.position);
	vis.packed_z = pack_f16_2(float2(z, length(prev_cam_pos)));
	gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;

	// calculate prev_uv
	float4 prevScreenPos = gPrevViews[view_index].projection.project_point(prev_cam_pos);
	prevScreenPos.y = -prevScreenPos.y;
	prevScreenPos.xyz /= prevScreenPos.w;
	gPrevUVs[path.pixel_coord.xy] = prevScreenPos.xy*.5 + .5;
	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePrevUV) gDebugImage[path.pixel_coord] = float4((prevScreenPos.xy*.5 + .5) - uv, 0, 1);

	// evaluate albedo and emission
	if (!gHasMedia || path._isect.sd.shape_area > 0) {
		Material _material;
		_material.load_and_sample(material_address, path._isect.sd.uv, path._isect.sd.uv_screen_size);

		gAlbedo[path.pixel_coord] = float4(_material.diffuse_reflectance, 1);
		if (gDemodulateAlbedo) {
			if (_material.diffuse_reflectance.r > 0) path._beta.r /= _material.diffuse_reflectance.r;
			if (_material.diffuse_reflectance.g > 0) path._beta.g /= _material.diffuse_reflectance.g;
			if (_material.diffuse_reflectance.b > 0) path._beta.b /= _material.diffuse_reflectance.b;
		}

		path.eval_emission(_material);

		if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eAlbedo) 		gDebugImage[path.pixel_coord] = float4(_material.diffuse_reflectance, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eDiffuse) 		gDebugImage[path.pixel_coord] = float4(_material.diffuse_reflectance, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eSpecular) 	gDebugImage[path.pixel_coord] = float4(_material.specular_reflectance, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eTransmission) gDebugImage[path.pixel_coord] = float4(_material.specular_transmittance.xxx, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eRoughness) 	gDebugImage[path.pixel_coord] = float4(sqrt(_material.alpha).xxx, 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEmission) 	gDebugImage[path.pixel_coord] = float4(_material.emission, 1);
	}

	if (any(path._beta > 0)) {
		PathState ps;
		ps.p.local_position = path.local_position;
		ps.p.instance_primitive_index = path._isect.instance_primitive_index;
		ps.origin = path.origin;
		ps.pack_path_length_medium(2, path._medium);
		ps.beta = path._beta;
		ps.bsdf_pdf = path.bsdf_pdf;
		ps.dir_in = path.direction;
		gPathStates[path.path_index] = ps;
	} else
		gPathStates[path.path_index].beta = 0;
}

#define _path_state gPathStates[path_index]
void load_path_state(inout PathIntegrator path, const uint path_index, const uint2 pixel_coord) {
	path.pixel_coord = pixel_coord.xy;
	path.path_index = path_index;
	path.path_length = _path_state.path_length();
	path._medium = _path_state.medium();
	path._beta = _path_state.beta;
	path.bsdf_pdf = _path_state.bsdf_pdf;
	path.origin = _path_state.origin;
	path.direction = _path_state.dir_in;
	path.local_position = _path_state.p.local_position;
	path._isect.instance_primitive_index = _path_state.p.instance_primitive_index;
	make_shading_data(path._isect.sd, _path_state.p.instance_index(), _path_state.p.primitive_index(), _path_state.p.local_position);
	path._isect.shape_pdf = shape_pdf(path.origin, path._isect.sd.shape_area, _path_state.p, path._isect.shape_pdf_area_measure);

	path.T_nee_pdf = 1;

	// handle miss
	if (path._isect.instance_index() == INVALID_INSTANCE) {
		path.G = 1;
		path.local_dir_in = path.direction;
		// update ray differential
		if (gSpecializationFlags & BDPT_FLAG_RAY_CONES)
			path._isect.sd.uv_screen_size *= gRayDifferentials[path_index].radius;
		return;
	}

	const Vector3 dp = path._isect.sd.position - path.origin;
	const Real dist2 = dot(dp, dp);
	path.G = 1/dist2;

	// update ray differential
	if (gSpecializationFlags & BDPT_FLAG_RAY_CONES) {
		RayDifferential ray_differential = gRayDifferentials[path_index];
		ray_differential.transfer(sqrt(dist2));
		path._isect.sd.uv_screen_size *= ray_differential.radius;
		gRayDifferentials[path_index] = ray_differential;
	}

	path._isect.sd.flags = 0;

	if (!gHasMedia || path._isect.sd.shape_area > 0) {
		path.local_dir_in = normalize(path._isect.sd.to_local(-path.direction));
		const Real cos_theta = dot(path.direction, path._isect.sd.geometry_normal());
		path.G *= abs(cos_theta);
		if (cos_theta > 0)
			path._isect.sd.flags |= SHADING_FLAG_FRONT_FACE;
	} else
		path.local_dir_in = path.direction;

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
	l.pdfA = ls.pdf;
	gPresampledLights[index.x] = l;
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void multi_kernel(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	PathIntegrator path;

	load_path_state(path, path_index, pixel_coord.xy);
	if (path.path_length > PathIntegrator::gMaxVertices || all(path._beta <= 0)) return;

	path.next_vertex();

	// store path state for next bounce
	if (any(path._beta > 0)) {
		PathState ps;
		ps.p.local_position = path.local_position;
		ps.p.instance_primitive_index = path._isect.instance_primitive_index;
		ps.origin = path.origin;
		ps.pack_path_length_medium(path.path_length, path._medium);
		ps.beta = path._beta;
		ps.bsdf_pdf = path.bsdf_pdf;
		ps.dir_in = path.direction;
		gPathStates[path.path_index] = ps;
	} else
		gPathStates[path.path_index].beta = 0;
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void single_kernel(uint3 pixel_coord : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_thread_index(pixel_coord.xy, group_id.xy, group_thread_index);
	if (path_index == -1) return;

	PathIntegrator path;

	load_path_state(path, path_index, pixel_coord.xy);

	while (path.path_length <= PathIntegrator::gMaxVertices && any(path._beta > 0))
		path.next_vertex();

	// store path state for next bounce
	if (any(path._beta > 0)) {
		PathState ps;
		ps.p.local_position = path.local_position;
		ps.p.instance_primitive_index = path._isect.instance_primitive_index;
		ps.origin = path.origin;
		ps.pack_path_length_medium(path.path_length, path._medium);
		ps.beta = path._beta;
		ps.bsdf_pdf = path.bsdf_pdf;
		ps.dir_in = path.direction;
		gPathStates[path.path_index] = ps;
	} else
		gPathStates[path.path_index].beta = 0;
}