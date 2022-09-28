#if 0
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry trace_shadows
#pragma compile slangc -profile sm_6_6 -lang slang -entry presample_lights
#pragma compile slangc -profile sm_6_6 -lang slang -entry add_light_trace
#pragma compile slangc -profile sm_6_6 -lang slang -entry hashgrid_compute_indices
#pragma compile slangc -profile sm_6_6 -lang slang -entry hashgrid_swizzle
#endif

//#define FORCE_LAMBERTIAN

#include "../../scene.h"
#include "../../bdpt.h"

#ifndef gSceneFlags
#define gSceneFlags 0
#endif
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

#ifdef HASHGRID_RESERVOIR_VERTEX
typedef PathVertex ReservoirPayload;
#else
typedef PresampledLightPoint ReservoirPayload;
#endif

#define gCoherentRNG 0

#define gHasEnvironment                (gSceneFlags & BDPT_FLAG_HAS_ENVIRONMENT)
#define gHasEmissives                  (gSceneFlags & BDPT_FLAG_HAS_EMISSIVES)
#define gHasMedia                      (gSceneFlags & BDPT_FLAG_HAS_MEDIA)
#define gTraceLight                    (gSceneFlags & BDPT_FLAG_TRACE_LIGHT)

#define gUsePerformanceCounters        BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::ePerformanceCounters)
#define gRemapThreadIndex              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eRemapThreads)
#define gCoherentRR                    BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eCoherentRR)
#define gCoherentSampling              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eCoherentSampling)
#define gFlipTriangleUVs               BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eFlipTriangleUVs)
#define gFlipNormalMaps	               BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eFlipNormalMaps)
#define gAlphaTest                     BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eAlphaTest)
#define gUseNormalMaps                 BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eNormalMaps)
#define gShadingNormalFix              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eShadingNormalShadowFix)
#define gUseRayCones                   BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eRayCones)
#define gSampleBSDFs                   BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eSampleBSDFs)
#define gSampleLightPower              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eSampleLightPower)
#define gUniformSphereSampling         BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eUniformSphereSampling)
#define gUseMIS                        BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eMIS)
#define gConnectToLights               BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eNEE)
#define gUseNEEReservoirs              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eNEEReservoirs)
#define gUseNEEReservoirReuse          BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eNEEReservoirReuse)
#define gPresampleLights               BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::ePresampleLights)
#define gDeferShadowRays               BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eDeferShadowRays)
#define gConnectToViews                BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eConnectToViews)
#define gConnectToLightPaths           BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eConnectToLightPaths)
#define gLightVertexCache              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eLVC)
#define gUseLVCReservoirs              BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eLVCReservoirs)
#define gUseLVCReservoirReuse          BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eLVCReservoirReuse)
#define gHashGridJitter                BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eHashGridJitter)
#define gSampleEnvironmentMap          BDPT_CHECK_FLAG(gSpecializationFlags, BDPTFlagBits::eSampleEnvironmentMapDirectly)

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
#define gReservoirM                    gPushConstants.gReservoirM
#define gReservoirMaxM                 gPushConstants.gReservoirMaxM
#define gReservoirSpatialM             gPushConstants.gReservoirSpatialM
#define gLightPathCount                gPushConstants.gLightPathCount
#define gHashGridBucketCount           gPushConstants.gHashGridBucketCount
#define gHashGridMinBucketRadius       gPushConstants.gHashGridMinBucketRadius
#define gHashGridBucketPixelRadius     gPushConstants.gHashGridBucketPixelRadius

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
[[vk::binding(11,1)]] RWStructuredBuffer<RayDifferential> gRayDifferentials;
[[vk::binding(12,1)]] RWStructuredBuffer<PresampledLightPoint> gPresampledLights;
[[vk::binding(13,1)]] RWStructuredBuffer<ShadowRayData> gShadowRays;
[[vk::binding(14,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(15,1)]] RWStructuredBuffer<PathVertex> gLightPathVertices;
[[vk::binding(16,1)]] RWStructuredBuffer<uint> gLightPathVertexCount;

[[vk::binding(17,1)]] RWStructuredBuffer<uint> gHashGridChecksums;
[[vk::binding(18,1)]] RWStructuredBuffer<uint> gHashGridCounters;
[[vk::binding(19,1)]] RWStructuredBuffer<uint> gHashGridStats;
[[vk::binding(20,1)]] RWStructuredBuffer<ReservoirData> gHashGridReservoirs;
[[vk::binding(21,1)]] RWStructuredBuffer<ReservoirPayload> gHashGridReservoirSamples;
[[vk::binding(22,1)]] RWStructuredBuffer<uint> gHashGridIndices;
[[vk::binding(23,1)]] RWStructuredBuffer<uint2> gHashGridAppendIndices;
[[vk::binding(24,1)]] RWStructuredBuffer<ReservoirData> gHashGridAppendReservoirs;
[[vk::binding(25,1)]] RWStructuredBuffer<ReservoirPayload> gHashGridAppendReservoirSamples;
[[vk::binding(26,1)]] RWStructuredBuffer<uint> gPrevHashGridChecksums;
[[vk::binding(27,1)]] RWStructuredBuffer<uint> gPrevHashGridCounters;
[[vk::binding(28,1)]] RWStructuredBuffer<uint> gPrevHashGridIndices;
[[vk::binding(29,1)]] RWStructuredBuffer<ReservoirData> gPrevHashGridReservoirs;
[[vk::binding(30,1)]] RWStructuredBuffer<ReservoirPayload> gPrevHashGridReservoirSamples;

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

#include "../../common/hashgrid.hlsli"
HashGrid<ReservoirPayload> gHashGrid = {
	gHashGridChecksums,
	gHashGridCounters,
	gHashGridReservoirs,
	gHashGridReservoirSamples,
	gHashGridIndices,
	gHashGridAppendIndices,
	gHashGridAppendReservoirs,
	gHashGridAppendReservoirSamples,
	gHashGridStats
};
HashGrid<ReservoirPayload> gPrevHashGrid = {
	gPrevHashGridChecksums,
	gPrevHashGridCounters,
	gPrevHashGridReservoirs,
	gPrevHashGridReservoirSamples,
	gPrevHashGridIndices,
	gHashGridAppendIndices,
	gHashGridAppendReservoirs,
	gHashGridAppendReservoirSamples,
	gHashGridStats
};
#define gLVCHashGrid gHashGrid
#define gPrevLVCHashGrid gPrevHashGrid
#define gNEEHashGrid gHashGrid
#define gPrevNEEHashGrid gPrevHashGrid
//ParameterBlock<HashGrid<ReservoirPayload>> gHashGrid;
//ParameterBlock<HashGrid<ReservoirPayload>> gPrevHashGrid;

uint hashgrid_lookup(RWStructuredBuffer<uint> checksums, const float3 pos, const float cell_size, const bool inserting = false) {
	uint checksum;
	uint bucket_index = hashgrid_bucket_index(pos, checksum, cell_size);
	for (uint i = 0; i < 32; i++) {
		if (inserting) {
			uint checksum_prev;
			InterlockedCompareExchange(checksums[bucket_index], 0, checksum, checksum_prev);
			if (checksum_prev == 0 || checksum_prev == checksum)
				return bucket_index;
		} else {
			if (checksums[bucket_index] == checksum)
				return bucket_index;
		}
		bucket_index++;
	}
	if (gUsePerformanceCounters && inserting)
		InterlockedAdd(gHashGridStats[0], 1); // failed inserts
	return -1;
}
void hashgrid_insert(const float3 pos, const float cell_size, const ReservoirData rd, const ReservoirPayload y) {
	const uint bucket_index = hashgrid_lookup(gHashGridChecksums, pos, cell_size, true);
	if (bucket_index == -1) return;

	uint index_in_bucket;
	InterlockedAdd(gHashGridCounters[bucket_index], 1, index_in_bucket);

	if (gUsePerformanceCounters && index_in_bucket == 0)
		InterlockedAdd(gHashGridStats[1], 1); // buckets used

	uint append_index;
	InterlockedAdd(gHashGridAppendIndices[0][0], 1, append_index);
	gHashGridAppendReservoirs[1 + append_index] = rd;
	gHashGridAppendReservoirSamples[1 + append_index] = y;
	gHashGridAppendIndices[1 + append_index] = uint2(bucket_index, index_in_bucket);
}
SLANG_SHADER("compute")
[numthreads(64,1,1)]
void hashgrid_compute_indices(uint3 index : SV_DispatchThreadID) {
	const uint bucket_index = index.y * gOutputExtent.x + index.x;
	if (bucket_index >= gHashGridBucketCount) return;

	uint offset;
	InterlockedAdd(gHashGridAppendIndices[0][1], gHashGridCounters[bucket_index], offset);

	gHashGridIndices[bucket_index] = offset;
}
SLANG_SHADER("compute")
[numthreads(64,1,1)]
void hashgrid_swizzle(uint3 index : SV_DispatchThreadID) {
	const uint append_index = index.y * gOutputExtent.x + index.x;
	if (append_index >= gHashGridAppendIndices[0][0]) return;
	const uint2 data = gHashGridAppendIndices[1 + append_index];
	const uint bucket_index = data[0];
	const uint index_in_bucket = data[1];

	const uint dst_index = gHashGridIndices[bucket_index] + index_in_bucket;
	gHashGridReservoirs[dst_index]       = gHashGridAppendReservoirs[1 + append_index];
	gHashGridReservoirSamples[dst_index] = gHashGridAppendReservoirSamples[1 + append_index];
}



#include "../../common/rng.hlsli"
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
	path.path_pdf_rev = 1;
	path.dVC = 1 / ls.pdf;
	path.G = 1;
	path.prev_cos_out = 1;
	path.bsdf_pdf = ls.pdf;

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

	path._beta = 1;
	path._medium = gViewMediumInstances[view_index];

	// trace visibility ray
	path.trace();

	path.path_contrib = 1;
	path.path_pdf = 1;
	path.path_pdf_rev = 1;
	path.bsdf_pdf = 1;
	path.G = 1;

	// dE_1 = N_{0,k} / p_0_fwd
	path.dVC = 1 / pdfWtoA(path.bsdf_pdf, path.G);

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
		if (gHasEnvironment) {
			Environment env;
			env.load(gEnvironmentMaterialAddress);
			path.eval_emission(env.eval(path.direction));
		}
		return;
	}

	// evaluate albedo and emission
	if (!gHasMedia || path._isect.sd.shape_area > 0) {
		ShadingData tmp_sd = path._isect.sd;

		Material m;
		m.load(gInstances[path._isect.instance_index()].material_address(), tmp_sd);

		vis.packed_normal = tmp_sd.packed_shading_normal;

		path.eval_emission(m.Le());

		gAlbedo[path.pixel_coord] = float4(m.albedo(), 1);

		if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eAlbedo) 		  gDebugImage[path.pixel_coord] = float4(m.albedo(), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eSpecular) 	  gDebugImage[path.pixel_coord] = float4(float3(m.is_specular()), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEmission) 	  gDebugImage[path.pixel_coord] = float4(m.Le(), 1);
	    else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eShadingNormal)  gDebugImage[path.pixel_coord] = float4(tmp_sd.shading_normal() *.5+.5, 1);
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

	while (any(path._beta > 0) && !any(isnan(path._beta)))
		path.next_vertex();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void trace_shadows(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	if (path_index >= gOutputExtent.x*gOutputExtent.y) return;

	const uint2 pixel_coord = index.xy;

	Spectrum c = 0;
	for (int i = 1; i <= gMaxDiffuseVertices; i++) {
		ShadowRayData rd = gShadowRays[shadow_ray_index(path_index, i)];
		if (all(rd.contribution <= 0)) continue;

		rng_state_t _rng = rng_init(pixel_coord, rd.rng_offset);

		Real dir_pdf = 1;
		Real nee_pdf = 1;
		trace_visibility_ray(_rng, rd.ray_origin, rd.ray_direction, rd.ray_distance, rd.medium, rd.contribution, dir_pdf, nee_pdf);
		if (nee_pdf > 0) rd.contribution /= nee_pdf;

		c += rd.contribution;
	}

	gRadiance[pixel_coord] += float4(c, 0);
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void add_light_trace(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	if (all(index.xy < gOutputExtent) && (BDPTDebugMode)gDebugMode != BDPTDebugMode::eViewTraceContribution) {
		Spectrum c = load_light_sample(index.xy);
		if (any(c < 0) || any(isnan(c))) c = 0;
		gRadiance[index.xy] += float4(c, 0);
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eLightTraceContribution || ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1))
			gDebugImage[index.xy] = float4(c, 1);
	}
}