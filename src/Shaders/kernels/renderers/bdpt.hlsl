#if 0
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_visibility
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry sample_photons
#pragma compile slangc -capability GL_EXT_ray_tracing -profile sm_6_6 -lang slang -entry trace_shadows
#pragma compile slangc -profile sm_6_6 -lang slang -entry presample_lights
#pragma compile slangc -profile sm_6_6 -lang slang -entry add_light_trace
#pragma compile slangc -profile sm_6_6 -lang slang -entry hashgrid_compute_indices
#pragma compile slangc -profile sm_6_6 -lang slang -entry hashgrid_swizzle
#endif

#define GROUPSIZE_X 8
#define GROUPSIZE_Y 4

#include "../../scene.h"
#include "../../bdpt.h"
#include "../../common/bdpt_util.hlsli"
#include "../../common/hashgrid.hlsli"

struct SceneParameters {
	RaytracingAccelerationStructure gAccelerationStructure;
	StructuredBuffer<PackedVertexData> gVertices;
	ByteAddressBuffer gIndices;
	StructuredBuffer<InstanceData> gInstances;
	StructuredBuffer<TransformData> gInstanceTransforms;
	StructuredBuffer<TransformData> gInstanceInverseTransforms;
	StructuredBuffer<TransformData> gInstanceMotionTransforms;
	ByteAddressBuffer gMaterialData;
	StructuredBuffer<uint> gLightInstances;
	StructuredBuffer<float> gDistributions;
	SamplerState gStaticSampler;
	RWStructuredBuffer<uint> gRayCount;
	StructuredBuffer<uint> gVolumes[gVolumeCount];
	Texture2D<float4> gImages[gImageCount];
	Texture2D<float> gImage1s[gImageCount];
};
struct PerFrameParameters {
	StructuredBuffer<ViewData> gViews;
	StructuredBuffer<ViewData> gPrevViews;
	StructuredBuffer<TransformData> gViewTransforms;
	StructuredBuffer<TransformData> gInverseViewTransforms;
	StructuredBuffer<TransformData> gPrevInverseViewTransforms;
	StructuredBuffer<uint> gViewMediumInstances;
	RWTexture2D<float4> gRadiance;
	RWTexture2D<float4> gAlbedo;
	RWTexture2D<float4> gDebugImage;
	RWStructuredBuffer<VisibilityInfo> gVisibility;
	RWStructuredBuffer<DepthInfo> gDepth;
	RWTexture2D<float2> gPrevUVs;
	RWStructuredBuffer<PresampledLightPoint> gPresampledLights;
	RWStructuredBuffer<ShadowRayData> gShadowRays;
	RWByteAddressBuffer gLightTraceSamples;
	RWStructuredBuffer<PathVertex> gLightPathVertices;
	RWStructuredBuffer<uint> gLightPathVertexCount;

	HashGrid<NEEReservoir> gNEEHashGrid;
	HashGrid<NEEReservoir> gPrevNEEHashGrid;
	HashGrid<PathVertexReservoir> gLVCHashGrid;
	HashGrid<PathVertexReservoir> gPrevLVCHashGrid;
};

ParameterBlock<SceneParameters> gSceneParams;
ParameterBlock<PerFrameParameters> gFrameParams;

[[vk::push_constant]] ConstantBuffer<BDPTPushConstants> gPushConstants;

#include "../../common/rng.hlsli"
#include "../../common/path.hlsli"

SLANG_SHADER("compute")
[numthreads(64,1,1)]
void hashgrid_compute_indices(uint3 index : SV_DispatchThreadID) {
	const uint bucket_index = index.y * gOutputExtent.x + index.x;
	if (gUseNEEReservoirReuse) gFrameParams.gNEEHashGrid.compute_indices(bucket_index);
	if (gUseLVCReservoirReuse) gFrameParams.gLVCHashGrid.compute_indices(bucket_index);
}
SLANG_SHADER("compute")
[numthreads(64,1,1)]
void hashgrid_swizzle(uint3 index : SV_DispatchThreadID) {
	const uint append_index = index.y * gOutputExtent.x + index.x;
	if (gUseNEEReservoirReuse) gFrameParams.gNEEHashGrid.swizzle(append_index);
	if (gUseLVCReservoirReuse) gFrameParams.gLVCHashGrid.swizzle(append_index);
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
	gFrameParams.gPresampledLights[index.x] = l;
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

	const uint view_index = get_view_index(index.xy, gFrameParams.gViews, gViewCount);
	if (view_index == -1) return;

	gFrameParams.gRadiance[path.pixel_coord] = float4(0,0,0,1);
	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution || (BDPTDebugMode)gDebugMode == BDPTDebugMode::eViewTraceContribution)
		gFrameParams.gDebugImage[path.pixel_coord] = float4(0,0,0,1);

	if (gMaxPathVertices < 2) return;

	// initialize ray
	const float2 uv = (path.pixel_coord + 0.5 - gFrameParams.gViews[view_index].image_min)/gFrameParams.gViews[view_index].extent();
	float2 clip_pos = 2*uv - 1;
	clip_pos.y = -clip_pos.y;
	const Vector3 local_dir_out = normalize(gFrameParams.gViews[view_index].projection.back_project(clip_pos));
	path.prev_cos_out = abs(local_dir_out.z);
	const TransformData t = gFrameParams.gViewTransforms[view_index];
	path.direction = normalize(t.transform_vector(local_dir_out));
	path.origin = Vector3(t.m[0][3], t.m[1][3], t.m[2][3] );

	// initialize ray differential
	if (gUseRayCones) {
		const ViewData view = gFrameParams.gViews[view_index];
		const float2 clip_pos_dx = 2*(path.pixel_coord + float2(1,0) + 0.5 - view.image_min)/view.extent() - 1;
		const float2 clip_pos_dy = 2*(path.pixel_coord + float2(0,1) + 0.5 - view.image_min)/view.extent() - 1;
		clip_pos_dx.y = -clip_pos_dx.y;
		clip_pos_dy.y = -clip_pos_dy.y;
		const Vector3 dir_dx = view.projection.back_project(clip_pos_dx);
		const Vector3 dir_dy = view.projection.back_project(clip_pos_dy);

		path.ray_differential.radius = 0;
		path.ray_differential.spread = min(length(dir_dx/dir_dx.z - local_dir_out/local_dir_out.z), length(dir_dy/dir_dy.z - local_dir_out/local_dir_out.z));
	}

	if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEnvironmentSampleTest) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		for (uint i = 0; i < 8; i++) {
			Real pdf;
			Vector3 dir;
			const Spectrum c = env.sample(float2(rng_next_float(path._rng),rng_next_float(path._rng)), dir, pdf);
			gFrameParams.gDebugImage[path.pixel_coord].rgb += 1024*pow(max(0, dot(dir, path.direction)), 1024);
		}
		return;
	} else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEnvironmentSamplePDF) {
		Environment env;
		env.load(gEnvironmentMaterialAddress);
		gFrameParams.gDebugImage[path.pixel_coord].rgb = env.eval_pdf(path.direction);
		return;
	}

	path._beta = 1;
	path._medium = gFrameParams.gViewMediumInstances[view_index];

	// trace visibility ray
	path.trace();

	path.path_contrib = 1;
	path.path_pdf = 1;
	path.path_pdf_rev = 1;
	path.bsdf_pdf = 1;
	path.G = 1;

	// dE_1 = N_{0,k} / p_0_fwd
	path.dVC = 1 / pdfWtoA(path.bsdf_pdf, path.G);

	if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eGeometryNormal) gFrameParams.gDebugImage[path.pixel_coord] = float4(path._isect.sd.geometry_normal()*.5+.5, 1);
	else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eShadingNormal)  gFrameParams.gDebugImage[path.pixel_coord] = float4(path._isect.sd.shading_normal() *.5+.5, 1);

	// store visibility
	VisibilityInfo vis;
	vis.instance_primitive_index = path._isect.instance_primitive_index;
	vis.packed_normal = path._isect.sd.packed_shading_normal;

	// handle miss
	if (path._isect.instance_index() == INVALID_INSTANCE) {
		gFrameParams.gAlbedo[path.pixel_coord] = 1;
		gFrameParams.gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;
		gFrameParams.gDepth[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = { POS_INFINITY, POS_INFINITY, 0 };
		gFrameParams.gPrevUVs[path.pixel_coord.xy] = uv;
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
		m.load(gSceneParams.gInstances[path._isect.instance_index()].material_address(), tmp_sd);

		vis.packed_normal = tmp_sd.packed_shading_normal;

		path.eval_emission(m.Le());

		gFrameParams.gAlbedo[path.pixel_coord] = float4(m.albedo(), 1);

		if      ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eAlbedo) 		 gFrameParams.gDebugImage[path.pixel_coord] = float4(m.albedo(), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eSpecular) 	 gFrameParams.gDebugImage[path.pixel_coord] = float4(float3(m.is_specular()), 1);
		else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eEmission) 	 gFrameParams.gDebugImage[path.pixel_coord] = float4(m.Le(), 1);
	    else if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eShadingNormal) gFrameParams.gDebugImage[path.pixel_coord] = float4(tmp_sd.shading_normal() *.5+.5, 1);
	}
	gFrameParams.gVisibility[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = vis;

	const Vector3 prev_cam_pos = tmul(gFrameParams.gPrevInverseViewTransforms[view_index], gSceneParams.gInstanceMotionTransforms[path._isect.instance_index()]).transform_point(path._isect.sd.position);

	// store depth information
	{
		DepthInfo depth;
		depth.z = length(path._isect.sd.position - path.origin);
		depth.prev_z = length(prev_cam_pos);

		const ViewData view = gFrameParams.gViews[view_index];
		const float2 uv_x = (path.pixel_coord + uint2(1,0) + 0.5 - view.image_min)/view.extent();
		float2 clipPos_x = 2*uv_x - 1;
		clipPos_x.y = -clipPos_x.y;
		const Vector3 dir_out_x = normalize(gFrameParams.gViewTransforms[view_index].transform_vector(normalize(view.projection.back_project(clipPos_x))));
		depth.dz_dxy.x = ray_plane(path.origin - path._isect.sd.position, dir_out_x, path._isect.sd.geometry_normal()) - depth.z;

		const float2 uv_y = (path.pixel_coord + uint2(0,1) + 0.5 - view.image_min)/view.extent();
		float2 clipPos_y = 2*uv_y - 1;
		clipPos_y.y = -clipPos_y.y;
		const Vector3 dir_out_y = normalize(gFrameParams.gViewTransforms[view_index].transform_vector(normalize(view.projection.back_project(clipPos_y))));
		depth.dz_dxy.y = ray_plane(path.origin - path._isect.sd.position, dir_out_y, path._isect.sd.geometry_normal()) - depth.z;

		gFrameParams.gDepth[path.pixel_coord.y*gOutputExtent.x + path.pixel_coord.x] = depth;
	}

	// calculate prev uv
	{
		float4 prev_clip_pos = gFrameParams.gPrevViews[view_index].projection.project_point(prev_cam_pos);
		prev_clip_pos.y = -prev_clip_pos.y;
		prev_clip_pos.xyz /= prev_clip_pos.w;
		gFrameParams.gPrevUVs[path.pixel_coord.xy] = prev_clip_pos.xy*.5 + .5;
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePrevUV)
			gFrameParams.gDebugImage[path.pixel_coord] = float4(abs(gFrameParams.gPrevUVs[path.pixel_coord.xy] - uv)*gOutputExtent, 0, 1);
	}

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
		ShadowRayData rd = gFrameParams.gShadowRays[shadow_ray_index(path_index, i)];
		if (all(rd.contribution <= 0)) continue;

		rng_state_t _rng = rng_init(pixel_coord, rd.rng_offset);

		Real dir_pdf = 1;
		Real nee_pdf = 1;
		trace_visibility_ray(_rng, rd.ray_origin, rd.ray_direction, rd.ray_distance, rd.medium, rd.contribution, dir_pdf, nee_pdf);
		if (nee_pdf > 0) rd.contribution /= nee_pdf;

		c += rd.contribution;
	}

	gFrameParams.gRadiance[pixel_coord] += float4(c, 0);
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void add_light_trace(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	if (all(index.xy < gOutputExtent) && (BDPTDebugMode)gDebugMode != BDPTDebugMode::eViewTraceContribution) {
		Spectrum c = load_light_sample(index.xy);
		if (any(c < 0) || any(isnan(c))) c = 0;
		gFrameParams.gRadiance[index.xy] += float4(c, 0);
		if ((BDPTDebugMode)gDebugMode == BDPTDebugMode::eLightTraceContribution || ((BDPTDebugMode)gDebugMode == BDPTDebugMode::ePathLengthContribution && gPushConstants.gDebugViewPathLength == 1))
			gFrameParams.gDebugImage[index.xy] = float4(c, 1);
	}
}