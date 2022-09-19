#if 0
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_visibility
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E sample_photons
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -HV 2021 -E trace_nee
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -T cs_6_7 -HV 2021 -E presample_lights
//#pragma compile dxc -Zpr -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -T cs_6_7 -HV 2021 -E add_light_trace
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry trace_nee
#pragma compile slangc -profile sm_6_6 -lang slang -entry presample_lights
#pragma compile slangc -profile sm_6_6 -lang slang -entry add_light_trace
#endif

//#define FORCE_LAMBERTIAN

#include "../../scene.h"
#include "../../bdpt.h"

#ifdef __SLANG__
#ifndef gSpecializationFlags
#define gSpecializationFlags 0
#endif
#ifndef gDebugMode
#define gDebugMode 0
#endif
#ifndef gLightTraceQuantization
#define gLightTraceQuantization 16384
#endif
[[vk::push_constant]] ConstantBuffer<BDPTPushConstants> gPushConstants;
#else // !defined(__SLANG__)
[[vk::constant_id(0)]] const uint gSpecializationFlags = 0;
[[vk::constant_id(1)]] const uint gDebugMode = 0;
[[vk::constant_id(2)]] const uint gLightTraceQuantization = 65536;
[[vk::push_constant]] const BDPTPushConstants gPushConstants;
#endif

#define gHasEnvironment                (gSpecializationFlags & BDPT_FLAG_HAS_ENVIRONMENT)
#define gHasEmissives                  (gSpecializationFlags & BDPT_FLAG_HAS_EMISSIVES)
#define gHasMedia                      (gSpecializationFlags & BDPT_FLAG_HAS_MEDIA)
#define gRemapThreadIndex              (gSpecializationFlags & BDPT_FLAG_REMAP_THREADS)
#define gCoherentRR                    (gSpecializationFlags & BDPT_FLAG_COHERENT_RR)
#define gCoherentRNG                   (gSpecializationFlags & BDPT_FLAG_COHERENT_RNG)
#define gFlipTriangleUVs               (gSpecializationFlags & BDPT_FLAG_FLIP_TRIANGLE_UVS)
#define gFlipNormalMaps	               (gSpecializationFlags & BDPT_FLAG_FLIP_NORMAL_MAPS)
#define gAlphaTest                     (gSpecializationFlags & BDPT_FLAG_ALPHA_TEST)
#define gUseNormalMaps                 (gSpecializationFlags & BDPT_FLAG_NORMAL_MAPS)
#define gShadingNormalFix              (gSpecializationFlags & BDPT_FLAG_SHADING_NORMAL_SHADOW_FIX)
#define gUseRayCones                   (gSpecializationFlags & BDPT_FLAG_RAY_CONES)
#define gSampleBSDFs                   (gSpecializationFlags & BDPT_FLAG_SAMPLE_BSDFS)
#define gSampleLightPower              (gSpecializationFlags & BDPT_FLAG_SAMPLE_LIGHT_POWER)
#define gUniformSphereSampling         (gSpecializationFlags & BDPT_FLAG_UNIFORM_SPHERE_SAMPLING)
#define gUseMIS                        (gSpecializationFlags & BDPT_FLAG_MIS)
#define gUseNEE                        (gSpecializationFlags & BDPT_FLAG_NEE)
#define gPresampleLights               (gSpecializationFlags & BDPT_FLAG_PRESAMPLE_LIGHTS)
#define gReservoirNEE                  (gSpecializationFlags & BDPT_FLAG_RESERVOIR_NEE)
#define gReservoirTemporalReuse        (gSpecializationFlags & BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE)
#define gReservoirSpatialReuse         (gSpecializationFlags & BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)
#define gReservoirUnbiasedReuse        (gSpecializationFlags & BDPT_FLAG_RESERVOIR_UNBIASED_REUSE)
#define gDeferNEERays                  (gSpecializationFlags & BDPT_FLAG_DEFER_NEE_RAYS)
#define gTraceLight                    (gSpecializationFlags & BDPT_FLAG_TRACE_LIGHT)
#define gConnectToViews                (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_VIEWS)
#define gConnectToLightPaths           (gSpecializationFlags & BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)
#define gLightVertexCache              (gSpecializationFlags & BDPT_FLAG_LIGHT_VERTEX_CACHE)
#define gLightVertexReservoirs         (gSpecializationFlags & BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS)
#define gSampleEnvironmentMap          (gSpecializationFlags & BDPT_FLAG_SAMPLE_ENV_TEXTURE)
#define gCountRays                     (gSpecializationFlags & BDPT_FLAG_COUNT_RAYS)

#define gOutputExtent                  gPushConstants.gOutputExtent
#define gViewCount                     gPushConstants.gViewCount
#define gLightCount                    gPushConstants.gLightCount
#define gLightDistributionPDF          gPushConstants.gLightDistributionPDF
#define gLightDistributionCDF          gPushConstants.gLightDistributionCDF
#define gEnvironmentMaterialAddress    gPushConstants.gEnvironmentMaterialAddress
#define gEnvironmentSampleProbability  gPushConstants.gEnvironmentSampleProbability
#define gRandomSeed                    gPushConstants.gRandomSeed
#define gMinPathVertices               gPushConstants.gMinPathVertices
#define gMaxPathVertices               gPushConstants.gMaxPathVertices
#define gMaxDiffuseVertices            gPushConstants.gMaxDiffuseVertices
#define gMaxNullCollisions             gPushConstants.gMaxNullCollisions
#define gLightPresampleTileSize        gPushConstants.gLightPresampleTileSize
#define gLightPresampleTileCount       gPushConstants.gLightPresampleTileCount
#define gNEEReservoirM                 gPushConstants.gNEEReservoirM
#define gReservoirMaxM                 gPushConstants.gReservoirMaxM
#define gNEEReservoirSpatialSamples    gPushConstants.gNEEReservoirSpatialSamples
#define gNEEReservoirSpatialRadius     gPushConstants.gNEEReservoirSpatialRadius
#define gLightPathCount                gPushConstants.gLightPathCount
#define gLightVertexReservoirM         gPushConstants.gLightVertexReservoirM

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
[[vk::binding(12+gVolumeCount,0)]] Texture2D<float4> gImages[gImageCount];
[[vk::binding(12+gVolumeCount+gImageCount,0)]] Texture2D<float> gImage1s[gImageCount];

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
[[vk::binding(12,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;

[[vk::binding(13,1)]] RWStructuredBuffer<PresampledLightPoint> gPresampledLights;
[[vk::binding(14,1)]] RWStructuredBuffer<NEERayData> gNEERays;

[[vk::binding(15,1)]] RWStructuredBuffer<Reservoir> gReservoirs;
[[vk::binding(16,1)]] RWStructuredBuffer<uint4> gReservoirSamples;
[[vk::binding(17,1)]] RWStructuredBuffer<Reservoir> gPrevReservoirs;
[[vk::binding(18,1)]] RWStructuredBuffer<uint4> gPrevReservoirSamples;

[[vk::binding(19,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(20,1)]] RWStructuredBuffer<PathVertex> gLightPathVertices;
[[vk::binding(21,1)]] RWStructuredBuffer<uint> gLightPathVertexCount;

float2 sample_texel(Texture2D<float4> img, float2 rnd, out float pdf, const uint max_iterations = 10) {
	static const uint2 offsets[4] = {
		uint2(0,0),
		uint2(1,0),
		uint2(0,1),
		uint2(1,1),
	};

 	uint2 full_size;
	uint level_count;
	img.GetDimensions(0, full_size.x, full_size.y, level_count);

	pdf = 1;
	uint2 coord = 0;
	uint2 last_size = 1;
 	for (uint i = 1; i < min(max_iterations+1, level_count-1); i++) {
		const uint level = level_count-1 - i;
		uint tmp;
		uint2 size;
		img.GetDimensions(level, size.x, size.y, tmp);
		coord *= size/last_size;

		const float inv_h = 1/(float)size.y;

		uint j;
		float4 p = 0;
		if (size.x > 1)
			for (j = 0; j < 2; j++)
				p[j] = luminance(img.Load(uint3(coord + offsets[j], level)).rgb) * sin(M_PI * (coord.y + offsets[j].y + 0.5f)*inv_h);
		if (size.y > 1)
			for (j = 2; j < 4; j++)
				p[j] = luminance(img.Load(uint3(coord + offsets[j], level)).rgb) * sin(M_PI * (coord.y + offsets[j].y + 0.5f)*inv_h);
		const float sum = dot(p, 1);
		if (sum < 1e-6) continue;
		p /= sum;

		for (j = 0; j < 4; j++) {
			if (rnd.x < p[j]) {
				coord += offsets[j];
				pdf *= p[j];
				rnd.x /= p[j];
				break;
			}
			rnd.x -= p[j];
		}
		last_size = size;
	}

	pdf *= last_size.x*last_size.y;

	return (float2(coord) + rnd) / float2(last_size);
}
float sample_texel_pdf(Texture2D<float4> img, const float2 uv, const uint max_iterations = 10) {
	static const uint2 offsets[4] = {
		uint2(0,0),
		uint2(1,0),
		uint2(0,1),
		uint2(1,1),
	};

 	uint2 full_size;
	uint level_count;
	img.GetDimensions(0, full_size.x, full_size.y, level_count);

	float pdf = 1;
	uint2 last_size = 1;
 	for (uint i = 1; i < min(max_iterations+1, level_count-1); i++) {
		const uint level = level_count-1 - i;
		uint tmp;
		uint2 size;
		img.GetDimensions(level, size.x, size.y, tmp);

		const uint2 coord = floor(size*uv/2)*2;

		const float inv_h = 1/(float)size.y;

		uint j;
		float4 p = 0;
		if (size.x > 1)
			for (j = 0; j < 2; j++)
				p[j] = luminance(img.Load(uint3(coord + offsets[j], level)).rgb) * sin(M_PI * (coord.y + offsets[j].y + 0.5f)*inv_h);
		if (size.y > 1)
			for (j = 2; j < 4; j++)
				p[j] = luminance(img.Load(uint3(coord + offsets[j], level)).rgb) * sin(M_PI * (coord.y + offsets[j].y + 0.5f)*inv_h);
		const float sum = dot(p, 1);
		if (sum < 1e-6) continue;
		p /= sum;

		const uint2 o = saturate(uint2(uv*size) - coord);
		pdf *= p[o.y*2 + o.x];

		last_size = size;
	}
	pdf *= last_size.x*last_size.y;
	return pdf;
}


#include "../../common/path.hlsli"

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4
uint map_pixel_coord(const uint2 pixel_coord, const uint2 group_id, const uint group_thread_index) {
	if (gRemapThreadIndex) {
		const uint dispatch_w = (gOutputExtent.x + GROUPSIZE_X - 1) / GROUPSIZE_X;
		const uint group_index = group_id.y*dispatch_w + group_id.x;
		return group_index*GROUPSIZE_X*GROUPSIZE_Y + group_thread_index;
	} else
		return pixel_coord.y*gOutputExtent.x + pixel_coord.x;
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
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index >= gLightPathCount) return;

	PathIntegrator path = PathIntegrator(index.xy, path_index);

	// sample point on light

	LightSampleRecord ls;
	sample_point_on_light(ls, float4(rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng), rng_next_float(path._rng)), 0);
	if (ls.pdf <= 0 || all(ls.radiance <= 0)) return;

	path._medium = -1;
	path._isect.sd.position = ls.position;
	path._isect.sd.packed_geometry_normal = pack_normal_octahedron(ls.normal);
	path._isect.sd.shape_area = 1; // just needs to be >0 so that its not treated as a volume sample

	path.path_contrib = ls.radiance;
	path._beta = ls.radiance / ls.pdf;
	path.path_pdf = ls.pdf;
	path.d = 1 / ls.pdf;
	path.G = 1;
	path.prev_cos_out = 1;
	path.bsdf_pdf = ls.pdf;
	path.prev_specular = false;

	if (gConnectToLightPaths) path.store_vertex();

	// sample direction
	const Vector3 local_dir_out = sample_cos_hemisphere(rng_next_float(path._rng), rng_next_float(path._rng));
	path.bsdf_pdf = cosine_hemisphere_pdfW(local_dir_out.z);

	// cosine term from light surface
	path._beta *= local_dir_out.z / path.bsdf_pdf;
	path.path_contrib *= local_dir_out.z;
	path.prev_cos_out = local_dir_out.z;

	Vector3 T,B;
	make_orthonormal(ls.normal, T, B);
	path.direction = T*local_dir_out.x + B*local_dir_out.y + ls.normal*local_dir_out.z;
	path.origin = ray_offset(ls.position, ls.normal);

	path.trace();

	while (any(path._beta > 0) && !any(isnan(path._beta)))
		path.next_vertex();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void sample_visibility(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index >= gOutputExtent.x*gOutputExtent.y) return;

	PathIntegrator path = PathIntegrator(index.xy, path_index);

	const uint view_index = get_view_index(index.xy, gViews, gViewCount);
	if (view_index == -1) return;

	gRadiance[path.pixel_coord] = float4(0,0,0,1);
	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution || (BDPTDebugMode)gDebugMode == BDPTDebugMode::eViewTraceContribution)
		gDebugImage[path.pixel_coord] = float4(0,0,0,1);

	if (gMaxPathVertices < 2) return;

	// initialize ray
	const float2 uv = (path.pixel_coord + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent();
	float2 clip_pos = 2*uv - 1;
	clip_pos.y = -clip_pos.y;
	const Vector3 local_dir_out = normalize(gViews[view_index].projection.back_project(clip_pos));
	path.prev_cos_out = abs(local_dir_out.z);
	const TransformData t = gViewTransforms[view_index];
	path.direction = normalize(t.transform_vector(local_dir_out));
	path.origin = Vector3(t.m[0][3], t.m[1][3], t.m[2][3] );
	path._medium = gViewMediumInstances[view_index];

	// initialize ray differential
	if (gUseRayCones) {
		const float2 clip_pos_dx = 2*(path.pixel_coord + float2(1,0) + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent() - 1;
		const float2 clip_pos_dy = 2*(path.pixel_coord + float2(0,1) + 0.5 - gViews[view_index].image_min)/gViews[view_index].extent() - 1;
		clip_pos_dx.y = -clip_pos_dx.y;
		clip_pos_dy.y = -clip_pos_dy.y;
		const Vector3 dir_dx = gViews[view_index].projection.back_project(clip_pos_dx);
		const Vector3 dir_dy = gViews[view_index].projection.back_project(clip_pos_dy);

		RayDifferential ray_differential;
		ray_differential.radius = 0;
		ray_differential.spread = min(length(dir_dx/dir_dx.z - local_dir_out/local_dir_out.z), length(dir_dy/dir_dy.z - local_dir_out/local_dir_out.z));
		gRayDifferentials[path.path_index] = ray_differential;
	}

	path._beta = 1;
	path.bsdf_pdf = gViews[view_index].sensor_pdfW(path.prev_cos_out);
	path.prev_specular = true;

	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEnvironmentSampleTest) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		for (uint i = 0; i < 8; i++) {
			Real pdf;
			Vector3 dir;
			const Spectrum c = env.sample(float2(rng_next_float(path._rng),rng_next_float(path._rng)), dir, pdf);
			gDebugImage[path.pixel_coord].rgb += 1024*pow(max(0, dot(dir, path.direction)), 1024);
		}
		return;
	} else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEnvironmentSamplePDF) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		gDebugImage[path.pixel_coord].rgb = env.eval_pdf(path.direction);
		return;
	}

	// trace visibility ray
	path.trace();

	path.path_contrib = 1;
	path.path_pdf = 1;
	path.bsdf_pdf = 1;
	path.G = 1;

	// dE_1 = N_{0,k} / p_0_fwd
	path.d = 1 / pdfWtoA(path.bsdf_pdf, path.G);

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
		if (gUseNEE && gReservoirNEE) gReservoirs[path.path_index].init();
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			path.eval_emission(env.eval(path.direction));
		}
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
		ShadingData tmp_sd = path._isect.sd;

		Material m;
		m.load(gInstances[path._isect.instance_index()].material_address(), tmp_sd);

		path.eval_emission(m.Le());

		gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x].packed_normal = tmp_sd.packed_shading_normal;

		gAlbedo[path.pixel_coord] = float4(m.albedo(), 1);

		if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eAlbedo) 		  gDebugImage[path.pixel_coord] = float4(m.albedo(), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eSpecular) 	  gDebugImage[path.pixel_coord] = float4(float3(m.is_specular()), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEmission) 	  gDebugImage[path.pixel_coord] = float4(m.Le(), 1);
	    else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eShadingNormal)  gDebugImage[path.pixel_coord] = float4(tmp_sd.shading_normal() *.5+.5, 1);
	}

	while (any(path._beta > 0) && !any(isnan(path._beta)))
		path.next_vertex();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void trace_nee(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index >= gOutputExtent.x*gOutputExtent.y) return;

	const uint2 pixel_coord = index.xy;

	Spectrum c = 0;
	for (int i = 1; i <= gMaxDiffuseVertices; i++) {
		const NEERayData rd = gNEERays[nee_vertex_index(path_index, i)];
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

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void add_light_trace(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	if (all(index.xy < gOutputExtent) && (BDPTDebugMode)gDebugMode != BDPTDebugMode::eViewTraceContribution) {
		Spectrum c = PathIntegrator::load_light_sample(index.xy);
		if (any(c < 0) || any(isnan(c))) c = 0;
		gRadiance[index.xy] += float4(c, 0);
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eLightTraceContribution || ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1))
			gDebugImage[index.xy] = float4(c, 1);
	}
}