// Direct port of https://github.com/SmallVCM/SmallVCM/blob/master/src/vertexcm.hxx

#if 0 // to avoid unknown #pragma warning
#pragma compile slangc -lang slang -profile sm_6_6 -capability GL_EXT_ray_tracing -entry light_trace
#pragma compile slangc -lang slang -profile sm_6_6 -capability GL_EXT_ray_tracing -entry camera_trace
#pragma compile slangc -lang slang -profile sm_6_6 -entry add_light_trace
#endif

#include "../../scene.h"
#include "../../vcm.h"

#ifndef gSpecializationFlags
#define gSpecializationFlags 0
#endif
#ifndef gLightTraceQuantization
#define gLightTraceQuantization 16384
#endif
#ifndef gDebugMode
#define gDebugMode 0
#endif
[[vk::push_constant]] ConstantBuffer<VCMPushConstants> gPushConstants;

[[vk::binding( 0,0)]] RaytracingAccelerationStructure gScene;
[[vk::binding( 1,0)]] StructuredBuffer<PackedVertexData> gVertices;
[[vk::binding( 2,0)]] ByteAddressBuffer gIndices;
[[vk::binding( 3,0)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding( 4,0)]] StructuredBuffer<TransformData> gInstanceTransforms;
[[vk::binding( 5,0)]] StructuredBuffer<TransformData> gInstanceInverseTransforms;
[[vk::binding( 6,0)]] StructuredBuffer<TransformData> gInstanceMotionTransforms;
[[vk::binding( 7,0)]] ByteAddressBuffer gMaterialData;
[[vk::binding( 8,0)]] StructuredBuffer<uint> gLightInstanceMap;
[[vk::binding( 9,0)]] StructuredBuffer<float> gDistributions;
[[vk::binding(10,0)]] SamplerState gSampler;
[[vk::binding(11,0)]] RWStructuredBuffer<uint> gRayCount;
[[vk::binding(12,0)]] StructuredBuffer<uint> gVolumes[gVolumeCount];
[[vk::binding(12+gVolumeCount,0)]] Texture2D<float4> gImages[gImageCount];

[[vk::binding( 0,1)]] StructuredBuffer<ViewData> gViews;
[[vk::binding( 1,1)]] StructuredBuffer<ViewData> gPrevViews;
[[vk::binding( 2,1)]] StructuredBuffer<TransformData> gViewTransforms;
[[vk::binding( 3,1)]] StructuredBuffer<TransformData> gInverseViewTransforms;
[[vk::binding( 4,1)]] StructuredBuffer<TransformData> gPrevInverseViewTransforms;
[[vk::binding( 5,1)]] StructuredBuffer<uint> gViewMediumInstances;
[[vk::binding( 6,1)]] RWTexture2D<float4> gRadiance;
[[vk::binding( 7,1)]] RWTexture2D<float4> gAlbedo;
[[vk::binding( 8,1)]] RWTexture2D<float2> gPrevUVs;
[[vk::binding( 9,1)]] RWTexture2D<float4> gDebugImage;
[[vk::binding(10,1)]] RWStructuredBuffer<VisibilityInfo> gVisibility;
[[vk::binding(11,1)]] StructuredBuffer<VisibilityInfo> gPrevVisibility;
[[vk::binding(12,1)]] RWByteAddressBuffer gLightTraceSamples;
[[vk::binding(13,1)]] RWStructuredBuffer<VcmVertex> gLightVertices;
[[vk::binding(14,1)]] RWStructuredBuffer<uint> gPathLengths;

#define gHasEnvironment   (gSpecializationFlags & VCM_FLAG_HAS_ENVIRONMENT)
#define gHasEmissives     (gSpecializationFlags & VCM_FLAG_HAS_EMISSIVES)
#define gHasMedia         (gSpecializationFlags & VCM_FLAG_HAS_MEDIA)
#define gRemapThreadIndex (gSpecializationFlags & VCM_FLAG_REMAP_THREADS)
#define gCountRays        (gSpecializationFlags & VCM_FLAG_COUNT_RAYS)
#define gUseVM            (gSpecializationFlags & VCM_FLAG_USE_VM)
#define gUseVC            (gSpecializationFlags & VCM_FLAG_USE_VC)
#define gPpm              (gSpecializationFlags & VCM_FLAG_USE_PPM)
#define gUseNEE           (gSpecializationFlags & VCM_FLAG_USE_NEE)
#define gUseMis           (gSpecializationFlags & VCM_FLAG_USE_MIS)
#define gLightTraceOnly   (gSpecializationFlags & VCM_FLAG_LIGHT_TRACE_ONLY)

#define gUseRayCones 0
#define gUniformSphereSampling 1
#define gSceneSphere float4(0,0,0,10)
#define VCM_LAMBERTIAN

#define gOutputExtent				  gPushConstants.gOutputExtent
#define gViewCount 					  gPushConstants.gViewCount
#define gLightCount 				  gPushConstants.gLightCount
#define gEnvironmentMaterialAddress   gPushConstants.gEnvironmentMaterialAddress
#define gRandomSeed 				  gPushConstants.gRandomSeed
#define gMinPathLength	 			  gPushConstants.gMinPathLength
#define gMaxPathLength	 			  gPushConstants.gMaxPathLength
#define gMaxNullCollisions 			  gPushConstants.gMaxNullCollisions
#define gLightPathCount 			  gPushConstants.gLightPathCount

#include "../../common/vcm_util.hlsli"

float GetPathWeight(const uint aPathLength) {
	return aPathLength <= 1 ? 1.f : 1.f / (aPathLength + 1);
}

class VertexCM {
    float mRadiusAlpha;       // Radius reduction rate parameter
    float mBaseRadius;        // Initial merging radius
    float mMisVmWeightFactor; // Weight of vertex merging (used in VC)
    float mMisVcWeightFactor; // Weight of vertex connection (used in VM)
    float mScreenPixelCount;  // Number of pixels
    float mLightSubPathCount; // Number of light sub-paths
    float mVmNormalization;   // 1 / (Pi * radius^2 * light_path_count)

    Rng mRng;

    // The sole point of this structure is to make carrying around the ray baggage easier.
    struct SubPathState {
        float3 mOrigin;             // Path origin
        float3 mDirection;          // Where to go next
        float3 mThroughput;         // Path throughput
        uint  mPathLength; // Number of path segments, including this
        bool  mIsFiniteLight; // Just generate by finite light
        bool  mSpecularPath; // All scattering events so far were specular

        float dVCM; // MIS quantity used for vertex connection and merging
        float dVC;  // MIS quantity used for vertex connection
        float dVM;  // MIS quantity used for vertex merging
    };

    // Path vertex, used for merging and connection
    struct PathVertex {
        float3 mHitpoint;   // Position of the vertex
        float3 mThroughput; // Path throughput (including emission)
        uint  mPathLength; // Number of segments between source and vertex

        // Stores all required local information, including incoming direction.
        AbstractBSDF mBsdf;

        float dVCM; // MIS quantity used for vertex connection and merging
        float dVC;  // MIS quantity used for vertex connection
        float dVM;  // MIS quantity used for vertex merging
    };

    typedef PathVertex CameraVertex;
    typedef PathVertex  LightVertex;

    typedef AbstractBSDF CameraBSDF;
    typedef AbstractBSDF  LightBSDF;

    // Range query used for PPM, BPT, and VCM. When HashGrid finds a vertex
    // within range -- Process() is called and vertex
    // merging is performed. BSDF of the camera vertex is used.
    class RangeQuery {
        float3       mCameraPosition;
        CameraBSDF   mCameraBsdf;
        SubPathState mCameraState;
        float3             mContrib;

        __init(
            const float3       aCameraPosition,
            const CameraBSDF   aCameraBsdf,
            const SubPathState aCameraState ) {
            mCameraPosition = aCameraPosition;
            mCameraBsdf = aCameraBsdf;
            mCameraState = aCameraState;
            mContrib = 0;
        }

        const float3 GetPosition() { return mCameraPosition; }
        const float3 GetContrib() { return mContrib; }

		SLANG_MUTATING
        void Process(LightVertex aLightVertex, const float aMisVcWeightFactor) {
            // Reject if full path length below/above min/max path length
            if ((aLightVertex.mPathLength + mCameraState.mPathLength > gMaxPathLength) ||
                (aLightVertex.mPathLength + mCameraState.mPathLength < gMinPathLength))
                 return;

            // Retrieve light incoming direction in world coordinates
            const float3 lightDirection = aLightVertex.mBsdf.WorldDirFix();

            float cosCamera, cameraBsdfDirPdfW, cameraBsdfRevPdfW;
            const float3 cameraBsdfFactor = mCameraBsdf.Evaluate(lightDirection, cosCamera, cameraBsdfDirPdfW, cameraBsdfRevPdfW);

            if (IsZero(cameraBsdfFactor))
                return;

            cameraBsdfDirPdfW *= mCameraBsdf.ContinuationProb();

            // Even though this is pdf from camera BSDF, the continuation probability
            // must come from light BSDF, because that would govern it if light path
            // actually continued
            cameraBsdfRevPdfW *= aLightVertex.mBsdf.ContinuationProb();

            // Partial light sub-path MIS weight [tech. rep. (38)]
            const float wLight = aLightVertex.dVCM * aMisVcWeightFactor +
                aLightVertex.dVM * Mis(cameraBsdfDirPdfW);

            // Partial eye sub-path MIS weight [tech. rep. (39)]
            const float wCamera = mCameraState.dVCM * aMisVcWeightFactor +
                mCameraState.dVM * Mis(cameraBsdfRevPdfW);

            // Full path MIS weight [tech. rep. (37)]. No MIS for PPM
            const float misWeight = gPpm ?
                1.f :
                1.f / (wLight + 1.f + wCamera);

            mContrib += misWeight * cameraBsdfFactor * aLightVertex.mThroughput;
        }
    };

	uint2 mPixelIdx;
	uint mPathIdx;

    __init(
		const uint2 aPixelIdx,
		const uint  aPathIdx,
        const float aRadiusFactor,
        const float aRadiusAlpha,
        int         aSeed = 1234) {
        mRng = Rng(aPixelIdx, aSeed);
		mPixelIdx = aPixelIdx;
		mPathIdx = aPathIdx;
        mBaseRadius  = aRadiusFactor * gSceneSphere.w;
        mRadiusAlpha = aRadiusAlpha;

        // While we have the same number of pixels (camera paths)
        // and light paths, we do keep them separate for clarity reasons
        const int pathCount = gOutputExtent.x * gOutputExtent.y;
        mScreenPixelCount = float(gOutputExtent.x * gOutputExtent.y);
        mLightSubPathCount   = float(gOutputExtent.x * gOutputExtent.y);

        // Setup our radius, 1st iteration has aIteration == 0, thus offset
        float radius = mBaseRadius;
        //radius /= pow(float(aIteration + 1), 0.5f * (1 - aRadiusAlpha));
        // Purely for numeric stability
        radius = max(radius, 1e-7f);
        const float radiusSqr = pow2(radius);

        // Factor used to normalise vertex merging contribution.
        // We divide the summed up energy by disk radius and number of light paths
        mVmNormalization = 1.f / (radiusSqr * M_PI * mLightSubPathCount);

        // MIS weight constant [tech. rep. (20)], with n_VC = 1 and n_VM = mLightPathCount
        const float etaVCM = (M_PI * radiusSqr) * mLightSubPathCount;
        mMisVmWeightFactor = gUseVM ? Mis(etaVCM)       : 0.f;
        mMisVcWeightFactor = gUseVC ? Mis(1.f / etaVCM) : 0.f;
	}


	uint GetLightVertexIndex(const uint aIndex) {
		return mPathIdx + aIndex*mLightSubPathCount;
	}
	void StoreLightPathVertex(const uint aIndex, const SubPathState aLightState, const IntersectionVertex aIsect, const float3 aLocalDirIn) {
		VcmVertex lv;
		lv.position = aIsect.sd.position;

		lv.material_address_flags = 0;
		if (gHasMedia && aIsect.sd.shape_area == 0)        lv.material_address_flags |= PATH_VERTEX_FLAG_IS_MEDIUM;
		if (aIsect.sd.flags & SHADING_FLAG_FLIP_BITANGENT) lv.material_address_flags |= PATH_VERTEX_FLAG_FLIP_BITANGENT;
		const uint material_address = gInstances[aIsect.instance_index()].material_address();
		BF_SET(lv.material_address_flags, material_address, 4, 28);

		lv.uv = aIsect.sd.uv;
		lv.pack_beta(aLightState.mThroughput);

		lv.packed_geometry_normal = aIsect.sd.packed_geometry_normal;
		lv.packed_shading_normal = aIsect.sd.packed_shading_normal;
		lv.packed_tangent = aIsect.sd.packed_tangent;
		lv.packed_local_dir_in = pack_normal_octahedron(aLocalDirIn);

		lv.path_length = aLightState.mPathLength;
		lv.dVCM = aLightState.dVCM;
		lv.dVC = aLightState.dVC;
		lv.dVM = aLightState.dVM;

		gLightVertices[GetLightVertexIndex(aIndex)] = lv;
	}
	PathVertex LoadLightPathVertex(const uint aIndex) {
		const VcmVertex lv = gLightVertices[GetLightVertexIndex(aIndex)];
		PathVertex v;
		v.mHitpoint = lv.position;
		v.mThroughput = lv.beta();
		v.mPathLength = lv.path_length;

		v.mBsdf.bsdf.load(lv.material_address(), lv.uv, 0);
		v.mBsdf.mAdjoint = true;
		v.mBsdf.mFlipBitangent = (lv.material_address_flags & PATH_VERTEX_FLAG_FLIP_BITANGENT) > 0;
		v.mBsdf.mPackedShadingNormal = lv.packed_shading_normal;
		v.mBsdf.mPackedGeometryNormal = lv.packed_geometry_normal;
		v.mBsdf.mPackedTangent = lv.packed_tangent;
		v.mBsdf.mLocalDirIn = lv.local_dir_in();
		v.mBsdf.mCosGeometryThetaIn = dot(v.mBsdf.WorldDirFix(), lv.geometry_normal());

		v.dVCM = lv.dVCM;
		v.dVC = lv.dVC;
		v.dVM = lv.dVM;

		return v;
	}

	SLANG_MUTATING
	void TraceLightPaths() {
		SubPathState lightState;
		GenerateLightSample(lightState);

		int count = 0;

		//////////////////////////////////////////////////////////////////////////
		// Trace light path
		for(;; ++lightState.mPathLength) {
			IntersectionVertex isect;
			float3 localHitPosition;
			const float dist = trace_ray(lightState.mOrigin, lightState.mDirection, POS_INFINITY, isect, localHitPosition);
			if (isect.instance_index() == INVALID_INSTANCE)
				break;

			LightBSDF bsdf = LightBSDF(lightState.mDirection, isect, true);
			if (!bsdf.IsValid() || !IsZero(bsdf.Le()))
				break;

			// Update the MIS quantities before storing them at the vertex.
			// These updates follow the initialization in GenerateLightSample() or
			// SampleScattering(), and together implement equations [tech. rep. (31)-(33)]
			// or [tech. rep. (34)-(36)], respectively.
			{
				// Infinite lights use MIS handled via solid angle integration,
				// so do not divide by the distance for such lights [tech. rep. Section 5.1]
				if(lightState.mPathLength > 1 || lightState.mIsFiniteLight == 1)
					lightState.dVCM *= Mis(pow2(dist));

				lightState.dVCM /= Mis(abs(bsdf.CosThetaFix()));
				lightState.dVC  /= Mis(abs(bsdf.CosThetaFix()));
				lightState.dVM  /= Mis(abs(bsdf.CosThetaFix()));
			}

			// Store vertex, unless BSDF is purely specular, which prevents
			// vertex connections and merging
			if (!bsdf.IsDelta() && (gUseVC || gUseVM)) {
				StoreLightPathVertex(count, lightState, isect, bsdf.mLocalDirIn);
				count++;
			}

			// Connect to camera, unless BSDF is purely specular
			if (!bsdf.IsDelta() && (gUseVC || gLightTraceOnly)) {
				if (lightState.mPathLength + 1 >= gMinPathLength)
					ConnectToCamera(lightState, isect.sd.position, bsdf);
			}

			// Terminate if the path would become too long after scattering
			if (lightState.mPathLength + 2 > gMaxPathLength)
				break;

			// Continue random walk
			if (!SampleScattering(bsdf, lightState))
				break;

			const float3 geometryNormal = bsdf.GetGeometryNormal();
			lightState.mOrigin = ray_offset(isect.sd.position, dot(geometryNormal, lightState.mDirection) < 0 ? -geometryNormal : geometryNormal);
		}

		gPathLengths[mPathIdx] = count;
	}

	// Unless rendering with traditional light tracing
	SLANG_MUTATING
	void TraceCameraPaths() {
		SubPathState cameraState;
		const float2 screenSample = GenerateCameraSample(cameraState);

		float3 color = 0;

		//////////////////////////////////////////////////////////////////////
		// Trace camera path
		for(;; ++cameraState.mPathLength) {
			IntersectionVertex isect;
			float3 localHitPosition;
			const float dist = trace_ray(cameraState.mOrigin, cameraState.mDirection, POS_INFINITY, isect, localHitPosition);
			if (isect.instance_index() == INVALID_INSTANCE) {
				if (cameraState.mPathLength == 1) {
					gAlbedo[mPixelIdx] = 1;
					gVisibility[mPixelIdx.y * gOutputExtent.x + mPixelIdx.x].instance_primitive_index = INVALID_INSTANCE | (INVALID_PRIMITIVE << 16);
				}
				// Get radiance from environment
				if (gHasEnvironment) {
					if (cameraState.mPathLength >= gMinPathLength) {
						AbstractBSDF bsdf;
						color += cameraState.mThroughput * GetLightRadiance(cameraState, isect, bsdf);
					}
				}
				break;
			}

			CameraBSDF bsdf = CameraBSDF(cameraState.mDirection, isect, false);
			if (!bsdf.IsValid())
				break;


			// store visibility, albedo, etc
			if (cameraState.mPathLength == 1) {
				VisibilityInfo vis;
				vis.instance_primitive_index = isect.instance_primitive_index;
				vis.packed_normal = isect.sd.packed_shading_normal;
				//const Vector3 prev_cam_pos = tmul(gPrevInverseViewTransforms[0], gInstanceMotionTransforms[isect.instance_index()]).transform_point(isect.sd.position);
				const Vector3 prev_cam_pos = gPrevInverseViewTransforms[0].transform_point(isect.sd.position);
				vis.packed_z = pack_f16_2(float2(dist, length(prev_cam_pos)));
				vis.packed_dz = pack_f16_2(1);
				gVisibility[mPixelIdx.y * gOutputExtent.x + mPixelIdx.x] = vis;
				gAlbedo[mPixelIdx] = float4(bsdf.bsdf.albedo(), 1);

				// calculate prev uv
				float4 prev_clip_pos = gPrevViews[0].projection.project_point(prev_cam_pos);
				prev_clip_pos.y = -prev_clip_pos.y;
				prev_clip_pos.xyz /= prev_clip_pos.w;
				gPrevUVs[mPixelIdx] = prev_clip_pos.xy*.5 + .5;

				if ((VCMDebugMode)gDebugMode == VCMDebugMode::eAlbedo)
					gDebugImage[mPixelIdx] = float4(bsdf.bsdf.albedo(), 1);
				else if ((VCMDebugMode)gDebugMode == VCMDebugMode::eEmission)
					gDebugImage[mPixelIdx] = float4(bsdf.Le(), 1);
				else if ((VCMDebugMode)gDebugMode == VCMDebugMode::eShadingNormal)
					gDebugImage[mPixelIdx] = float4(isect.sd.shading_normal() *.5+.5, 1);
				else if ((VCMDebugMode)gDebugMode == VCMDebugMode::eGeometryNormal)
					gDebugImage[mPixelIdx] = float4(isect.sd.geometry_normal()*.5+.5, 1);
				else if ((VCMDebugMode)gDebugMode == VCMDebugMode::ePrevUV)
					gDebugImage[mPixelIdx] = float4(prev_clip_pos.xy*.5 + .5, 0, 1);
			}

			// Update the MIS quantities, following the initialization in
			// GenerateLightSample() or SampleScattering(). Implement equations
			// [tech. rep. (31)-(33)] or [tech. rep. (34)-(36)], respectively.
			{
				cameraState.dVCM *= Mis(pow2(dist));
				cameraState.dVCM /= Mis(abs(bsdf.CosThetaFix()));
				cameraState.dVC  /= Mis(abs(bsdf.CosThetaFix()));
				cameraState.dVM  /= Mis(abs(bsdf.CosThetaFix()));
			}

			// Light source has been hit; terminate afterwards, since
			// our light sources do not have reflective properties
			if (bsdf.IsLight()) {
				if(cameraState.mPathLength >= gMinPathLength) {
					color += cameraState.mThroughput * GetLightRadiance(cameraState, isect, bsdf);
				}
				break;
			}

			// Terminate if eye sub-path is too long for connections or merging
			if (cameraState.mPathLength >= gMaxPathLength)
				break;

			////////////////////////////////////////////////////////////////
			// Vertex connection: Connect to a light source
			if (!bsdf.IsDelta() && gUseVC) {
				if (cameraState.mPathLength + 1>= gMinPathLength) {
					color += cameraState.mThroughput * DirectIllumination(cameraState, isect.sd.position, bsdf);
				}
			}

			////////////////////////////////////////////////////////////////
			// Vertex connection: Connect to light vertices
			if(!bsdf.IsDelta() && gUseVC) {
				// For VC, each light sub-path is assigned to a particular eye
				// sub-path, as in traditional BPT. It is also possible to
				// connect to vertices from any light path, but MIS should
				// be revisited.
				const uint range = gPathLengths[mPathIdx];

				for (int i = 0; i < range; i++) {
					const LightVertex lightVertex = LoadLightPathVertex(i);

					if (lightVertex.mPathLength + 1 + cameraState.mPathLength < gMinPathLength)
						continue;

					// Light vertices are stored in increasing path length
					// order; once we go above the max path length, we can
					// skip the rest
					if (lightVertex.mPathLength + 1 + cameraState.mPathLength > gMaxPathLength)
						break;

					color += cameraState.mThroughput * lightVertex.mThroughput * ConnectVertices(lightVertex, bsdf, isect.sd.position, cameraState);
				}
			}

			////////////////////////////////////////////////////////////////
			// Vertex merging: Merge with light vertices
			if (!bsdf.IsDelta() && gUseVM) {
				//RangeQuery query = RangeQuery(hitPoint, bsdf, cameraState);
				//mHashGrid.Process(mLightVertices, query);
				//color += cameraState.mThroughput * mVmNormalization * query.GetContrib();

				// PPM merges only at the first non-specular surface from camera
				if (gPpm) break;
			}

			if (!SampleScattering(bsdf, cameraState))
				break;

			const float3 geometryNormal = bsdf.GetGeometryNormal();
			cameraState.mOrigin = ray_offset(isect.sd.position, dot(geometryNormal, cameraState.mDirection) < 0 ? -geometryNormal : geometryNormal);

			if ((VCMDebugMode)gDebugMode == VCMDebugMode::eDirOut && cameraState.mPathLength == 1)
				gDebugImage[mPixelIdx] = float4(cameraState.mDirection*.5 + .5, 1);
		}

		gRadiance[mPixelIdx] = float4(color, 1);
	}

    //////////////////////////////////////////////////////////////////////////
    // Camera tracing methods
    //////////////////////////////////////////////////////////////////////////

    // Generates new camera sample given a pixel index
	SLANG_MUTATING
    float2 GenerateCameraSample(out SubPathState oCameraState) {
        const AbstractCamera camera = AbstractCamera(0);

        // Jitter pixel position
        const float2 s = mPixelIdx + mRng.GetVec2f();

        // Generate ray
		oCameraState.mOrigin = camera.mPosition;
		float cosAtCamera;
		oCameraState.mDirection = camera.GenerateRay(s, cosAtCamera);

        // Compute pdf conversion factor from area on image plane to solid angle on ray
        const float imagePointToCameraDist = camera.mImagePlaneDist / cosAtCamera;
        const float imageToSolidAngleFactor = pow2(imagePointToCameraDist) / cosAtCamera;

        // We put the virtual image plane at such a distance from the camera origin
        // that the pixel area is one and thus the image plane sampling pdf is 1.
        // The solid angle ray pdf is then equal to the conversion factor from
        // image plane area density to ray solid angle density
        const float cameraPdfW = imageToSolidAngleFactor;

        oCameraState.mThroughput   = float3(1);

        oCameraState.mPathLength   = 1;
        oCameraState.mSpecularPath = 1;

        // Eye sub-path MIS quantities. Implements [tech. rep. (31)-(33)] partially.
        // The evaluation is completed after tracing the camera ray in the eye sub-path loop.
        oCameraState.dVCM = Mis(mLightSubPathCount / cameraPdfW);
        oCameraState.dVC  = 0;
        oCameraState.dVM  = 0;

        return s;
    }

    // Returns the radiance of a light source when hit by a random ray,
    // multiplied by MIS weight. Can be used for both Background and Area lights.
    //
    // For Background lights:
    //    Has to be called BEFORE updating the MIS quantities.
    //    Value of aHitpoint is irrelevant (passing float3(0))
    //
    // For Area lights:
    //    Has to be called AFTER updating the MIS quantities.
    float3 GetLightRadiance(const SubPathState aCameraState, const IntersectionVertex aIsect, const AbstractBSDF aBsdf) {
        // We sample lights uniformly
        const int   lightCount    = gLightCount + (gHasEnvironment ? 1 : 0);
        const float lightPickProb = 1.f / lightCount;

        float directPdfA, emissionPdfW;
        const float3 radiance = InstanceLight::GetRadiance(aCameraState.mDirection, aIsect, aBsdf, directPdfA, emissionPdfW);

        if (IsZero(radiance))
            return float3(0);

        // If we see light source directly from camera, no weighting is required
        if (aCameraState.mPathLength == 1)
            return radiance;

        // When using only vertex merging, we want purely specular paths
        // to give radiance (cannot get it otherwise). Rest is handled
        // by merging and we should return 0.
        if (gUseVM && !gUseVC)
            return aCameraState.mSpecularPath ? radiance : float3(0);

        directPdfA   *= lightPickProb;
        emissionPdfW *= lightPickProb;

        // Partial eye sub-path MIS weight [tech. rep. (43)].
        // If the last hit was specular, then dVCM == 0.
        const float wCamera = Mis(directPdfA) * aCameraState.dVCM +
            Mis(emissionPdfW) * aCameraState.dVC;

        // Partial light sub-path weight is 0 [tech. rep. (42)].

        // Full path MIS weight [tech. rep. (37)].
        const float misWeight = (gUseVC || gUseVM) ? 1.f / (1.f + wCamera) : 1;

        return misWeight * radiance;
    }

    // Connects camera vertex to randomly chosen light point.
    // Returns emitted radiance multiplied by path MIS weight.
    // Has to be called AFTER updating the MIS quantities.
	SLANG_MUTATING
    float3 DirectIllumination(
        const SubPathState aCameraState,
        const float3       aHitpoint,
        const CameraBSDF   aBsdf) {
        // We sample lights uniformly
        const int   lightCount    = gLightCount + (gHasEnvironment ? 1 : 0);
        const float lightPickProb = 1.f / lightCount;

        const int    lightID       = int(mRng.GetFloat() * lightCount) % lightCount;
        const float3 rndPosSamples = mRng.GetVec3f();

        const InstanceLight light = InstanceLight(lightID);

        float3 directionToLight;
        float distance;
        float directPdfA, directPdfW, emissionPdfW, cosAtLight;
        const float3 radiance = light.Illuminate(aHitpoint, rndPosSamples, directionToLight, distance, directPdfA, directPdfW, emissionPdfW, cosAtLight);

        // If radiance == 0, other values are undefined, so have to early exit
        if (IsZero(radiance))
            return float3(0);

        float bsdfDirPdfW, bsdfRevPdfW, cosToLight;
        const float3 bsdfFactor = aBsdf.Evaluate(directionToLight, cosToLight, bsdfDirPdfW, bsdfRevPdfW);

        if (IsZero(bsdfFactor))
            return float3(0);

        const float continuationProbability = aBsdf.ContinuationProb();

        // If the light is delta light, we can never hit it
        // by BSDF sampling, so the probability of this path is 0
        bsdfDirPdfW *= light.IsDelta() ? 0.f : continuationProbability;

        bsdfRevPdfW *= continuationProbability;

        // Partial light sub-path MIS weight [tech. rep. (44)].
        // Note that wLight is a ratio of area pdfs. But since both are on the
        // light source, their distance^2 and cosine terms cancel out.
        // Therefore we can write wLight as a ratio of solid angle pdfs,
        // both expressed w.r.t. the same shading point.
        const float wLight = Mis(bsdfDirPdfW / (lightPickProb * directPdfW));

        // Partial eye sub-path MIS weight [tech. rep. (45)].
        //
        // In front of the sum in the parenthesis we have Mis(ratio), where
        //    ratio = emissionPdfA / directPdfA,
        // with emissionPdfA being the product of the pdfs for choosing the
        // point on the light source and sampling the outgoing direction.
        // What we are given by the light source instead are emissionPdfW
        // and directPdfW. Converting to area pdfs and plugging into ratio:
        //    emissionPdfA = emissionPdfW * cosToLight / dist^2
        //    directPdfA   = directPdfW * cosAtLight / dist^2
        //    ratio = (emissionPdfW * cosToLight / dist^2) / (directPdfW * cosAtLight / dist^2)
        //    ratio = (emissionPdfW * cosToLight) / (directPdfW * cosAtLight)
        //
        // Also note that both emissionPdfW and directPdfW should be
        // multiplied by lightPickProb, so it cancels out.
        const float wCamera = Mis(emissionPdfW * cosToLight / (directPdfW * cosAtLight)) * (
            mMisVmWeightFactor + aCameraState.dVCM + aCameraState.dVC * Mis(bsdfRevPdfW));

        // Full path MIS weight [tech. rep. (37)]
        const float misWeight = 1.f / (wLight + 1.f + wCamera);

        const float3 contrib =
            (misWeight * cosToLight / (lightPickProb * directPdfW)) * (radiance * bsdfFactor);

		const float3 geometryNormal = aBsdf.GetGeometryNormal();
        if (IsZero(contrib) || Occluded(ray_offset(aHitpoint, dot(directionToLight, geometryNormal) < 0 ? -geometryNormal : geometryNormal), directionToLight, distance*0.999))
            return float3(0);

        return contrib;
    }

    // Connects an eye and a light vertex. Result multiplied by MIS weight, but
    // not multiplied by vertex throughputs. Has to be called AFTER updating MIS
    // constants. 'direction' is FROM eye TO light vertex.
    float3 ConnectVertices(
        const LightVertex  aLightVertex,
        const CameraBSDF   aCameraBsdf,
        const float3       aCameraHitpoint,
        const SubPathState aCameraState) {
        // Get the connection
        float3 direction  = aLightVertex.mHitpoint - aCameraHitpoint;
        const float dist2 = dot(direction, direction);
        float distance    = sqrt(dist2);
        direction        /= distance;

        // Evaluate BSDF at camera vertex
        float cosCamera, cameraBsdfDirPdfW, cameraBsdfRevPdfW;
        const float3 cameraBsdfFactor = aCameraBsdf.Evaluate(direction, cosCamera, cameraBsdfDirPdfW, cameraBsdfRevPdfW);

        if (IsZero(cameraBsdfFactor))
            return float3(0);

        // Camera continuation probability (for Russian roulette)
        const float cameraCont = aCameraBsdf.ContinuationProb();
        cameraBsdfDirPdfW *= cameraCont;
        cameraBsdfRevPdfW *= cameraCont;

        // Evaluate BSDF at light vertex
        float cosLight, lightBsdfDirPdfW, lightBsdfRevPdfW;
        const float3 lightBsdfFactor = aLightVertex.mBsdf.Evaluate(-direction, cosLight, lightBsdfDirPdfW, lightBsdfRevPdfW);

        if (IsZero(lightBsdfFactor))
            return float3(0);

        // Light continuation probability (for Russian roulette)
        const float lightCont = aLightVertex.mBsdf.ContinuationProb();
        lightBsdfDirPdfW *= lightCont;
        lightBsdfRevPdfW *= lightCont;

        // Compute geometry term
        const float geometryTerm = cosLight * cosCamera / dist2;
        if(geometryTerm < 0)
            return float3(0);

        // Convert pdfs to area pdf
        const float cameraBsdfDirPdfA = PdfWtoA(cameraBsdfDirPdfW, distance, cosLight);
        const float lightBsdfDirPdfA  = PdfWtoA(lightBsdfDirPdfW,  distance, cosCamera);

        // Partial light sub-path MIS weight [tech. rep. (40)]
        const float wLight = Mis(cameraBsdfDirPdfA) * (
            mMisVmWeightFactor + aLightVertex.dVCM + aLightVertex.dVC * Mis(lightBsdfRevPdfW));

        // Partial eye sub-path MIS weight [tech. rep. (41)]
        const float wCamera = Mis(lightBsdfDirPdfA) * (
            mMisVmWeightFactor + aCameraState.dVCM + aCameraState.dVC * Mis(cameraBsdfRevPdfW));

        // Full path MIS weight [tech. rep. (37)]
        const float misWeight = 1.f / (wLight + 1.f + wCamera);

        const float3 contrib = (misWeight * geometryTerm) * cameraBsdfFactor * lightBsdfFactor;

		const float3 geometryNormal = aCameraBsdf.GetGeometryNormal();
        if (IsZero(contrib) || Occluded(ray_offset(aCameraHitpoint, dot(direction, geometryNormal) < 0 ? -geometryNormal : geometryNormal), direction, distance*0.999))
            return float3(0);

        return contrib;
    }

    //////////////////////////////////////////////////////////////////////////
    // Light tracing methods
    //////////////////////////////////////////////////////////////////////////

    // Samples light emission
	SLANG_MUTATING
    void GenerateLightSample(out SubPathState oLightState) {
        // We sample lights uniformly
        const int   lightCount    = gLightCount + (gHasEnvironment ? 1 : 0);
        const float lightPickProb = 1.f / lightCount;

        const int   lightID       = int(mRng.GetFloat() * lightCount) % lightCount;
        const float2 rndDirSamples = mRng.GetVec2f();
        const float3 rndPosSamples = mRng.GetVec3f();

        const InstanceLight light = InstanceLight(lightID);

        float emissionPdfW, directPdfA, cosLight;
        oLightState.mThroughput = light.Emit(rndDirSamples, rndPosSamples,
            oLightState.mOrigin, oLightState.mDirection,
            emissionPdfW, directPdfA, cosLight);

        emissionPdfW *= lightPickProb;
        directPdfA   *= lightPickProb;

        oLightState.mThroughput    /= emissionPdfW;
        oLightState.mPathLength    = 1;
        oLightState.mIsFiniteLight = light.IsFinite() ? 1 : 0;

        // Light sub-path MIS quantities. Implements [tech. rep. (31)-(33)] partially.
        // The evaluation is completed after tracing the emission ray in the light sub-path loop.
        // Delta lights are handled as well [tech. rep. (48)-(50)].
        {
            oLightState.dVCM = Mis(directPdfA / emissionPdfW);

            if(!light.IsDelta()) {
                const float usedCosLight = light.IsFinite() ? cosLight : 1.f;
                oLightState.dVC = Mis(usedCosLight / emissionPdfW);
            } else {
                oLightState.dVC = 0.f;
            }

            oLightState.dVM = oLightState.dVC * mMisVcWeightFactor;
        }
    }

    // Computes contribution of light sample to camera by splatting is onto the
    // framebuffer. Multiplies by throughput (obviously, as nothing is returned).
    void ConnectToCamera(
        const SubPathState aLightState,
        const float3       aHitpoint,
        const LightBSDF    aBsdf) {
        const AbstractCamera camera = AbstractCamera(0);
        float3 directionToCamera = camera.mPosition - aHitpoint;

        // Check point is in front of camera
        if (dot(camera.mForward, -directionToCamera) <= 0.f)
            return;

        // Check it projects to the screen (and where)
		float2 imagePos;
        if (!camera.WorldToRaster(aHitpoint, imagePos))
            return;

        // Compute distance and normalize direction to camera
        const float distEye2 = dot(directionToCamera, directionToCamera);
        const float distance = sqrt(distEye2);
        directionToCamera   /= distance;

        // Get the BSDF
        float cosToCamera, bsdfDirPdfW, bsdfRevPdfW;
        const float3 bsdfFactor = aBsdf.Evaluate(directionToCamera, cosToCamera, bsdfDirPdfW, bsdfRevPdfW);

        if (IsZero(bsdfFactor))
            return;

        bsdfRevPdfW *= aBsdf.ContinuationProb();

        // Compute pdf conversion factor from image plane area to surface area
        const float cosAtCamera = dot(camera.mForward, -directionToCamera);
        const float imagePointToCameraDist = camera.mImagePlaneDist / cosAtCamera;
        const float imageToSolidAngleFactor = pow2(imagePointToCameraDist) / cosAtCamera;
        const float imageToSurfaceFactor = imageToSolidAngleFactor * abs(cosToCamera) / pow2(distance);

        // We put the virtual image plane at such a distance from the camera origin
        // that the pixel area is one and thus the image plane sampling pdf is 1.
        // The area pdf of aHitpoint as sampled from the camera is then equal to
        // the conversion factor from image plane area density to surface area density
        const float cameraPdfA = imageToSurfaceFactor;

        // Partial light sub-path weight [tech. rep. (46)]. Note the division by
        // mLightPathCount, which is the number of samples this technique uses.
        // This division also appears a few lines below in the framebuffer accumulation.
        const float wLight = Mis(cameraPdfA / mLightSubPathCount) * (
            mMisVmWeightFactor + aLightState.dVCM + aLightState.dVC * Mis(bsdfRevPdfW));

        // Partial eye sub-path weight is 0 [tech. rep. (47)]

        // Full path MIS weight [tech. rep. (37)]. No MIS for traditional light tracing.
        const float misWeight = gLightTraceOnly ? 1.f : (1.f / (wLight + 1.f));

        const float surfaceToImageFactor = 1.f / imageToSurfaceFactor;

        // We divide the contribution by surfaceToImageFactor to convert the (already
        // divided) pdf from surface area to image plane area, w.r.t. which the
        // pixel integral is actually defined. We also divide by the number of samples
        // this technique makes, which is equal to the number of light sub-paths
        const float3 contrib = misWeight * aLightState.mThroughput * bsdfFactor /
            (mLightSubPathCount * surfaceToImageFactor);

        if (!IsZero(contrib)) {
            if (Occluded(camera.mPosition, -directionToCamera, distance*0.999))
                return;

            AddLightTraceContribution(imagePos, contrib);
        }
    }

    // Samples a scattering direction camera/light sample according to BSDF.
    // Returns false for termination
	SLANG_MUTATING
    bool SampleScattering(const AbstractBSDF aBsdf, inout SubPathState aoState) {
        // x,y for direction, z for component. No rescaling happens
        float3 rndTriplet  = mRng.GetVec3f();
        float bsdfDirPdfW, bsdfRevPdfW, cosThetaOut;
        bool specularEvent;

        const float3 bsdfFactor = aBsdf.Sample(rndTriplet, aoState.mDirection, bsdfDirPdfW, bsdfRevPdfW, cosThetaOut, specularEvent);

        if (IsZero(bsdfFactor))
            return false;

        // Russian roulette
        const float contProb = aBsdf.ContinuationProb();
        if (mRng.GetFloat() > contProb)
            return false;

        bsdfDirPdfW *= contProb;
        bsdfRevPdfW *= contProb;

        // Sub-path MIS quantities for the next vertex. Only partial - the
        // evaluation is completed when the actual hit point is known,
        // i.e. after tracing the ray, in the sub-path loop.

        if (specularEvent) {
            // Specular scattering case [tech. rep. (53)-(55)] (partially, as noted above)
            aoState.dVCM = 0.f;
            //aoState.dVC *= Mis(cosThetaOut / bsdfDirPdfW) * Mis(bsdfRevPdfW);
            //aoState.dVM *= Mis(cosThetaOut / bsdfDirPdfW) * Mis(bsdfRevPdfW);
            aoState.dVC *= Mis(cosThetaOut);
            aoState.dVM *= Mis(cosThetaOut);

            //aoState.mSpecularPath &= 1;
        } else {
            // Implements [tech. rep. (34)-(36)] (partially, as noted above)
            aoState.dVC = Mis(cosThetaOut / bsdfDirPdfW) * (
                aoState.dVC * Mis(bsdfRevPdfW) +
                aoState.dVCM + mMisVmWeightFactor);

            aoState.dVM = Mis(cosThetaOut / bsdfDirPdfW) * (
                aoState.dVM * Mis(bsdfRevPdfW) +
                aoState.dVCM * mMisVcWeightFactor + 1.f);

            aoState.dVCM = Mis(1.f / bsdfDirPdfW);

            aoState.mSpecularPath = 0;
        }

        aoState.mThroughput *= bsdfFactor * (cosThetaOut / bsdfDirPdfW);

        return true;
    }
};


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
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void light_trace(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	if (any(index.xy >= gOutputExtent)) return;
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	VertexCM vcm = VertexCM(index.xy, path_index, 1, 0.1);
	vcm.TraceLightPaths();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void camera_trace(uint3 index : SV_DispatchThreadID, uint group_thread_index : SV_GroupIndex, uint3 group_id : SV_GroupID) {
	if (any(index.xy >= gOutputExtent)) return;
	const uint path_index = map_pixel_coord(index.xy, group_id.xy, group_thread_index);
	gRadiance[index.xy] = float4(0,0,0,1);
	VertexCM vcm = VertexCM(index.xy, path_index, 1, 0.1);
	vcm.TraceCameraPaths();
}

SLANG_SHADER("compute")
[numthreads(GROUPSIZE_X,GROUPSIZE_Y,1)]
void add_light_trace(uint3 index : SV_DispatchThreadID) {
	if (any(index.xy >= gOutputExtent)) return;

	float3 lightTraceContribution = LoadLightTraceContribution(index.xy);

	if (any(isnan(lightTraceContribution)))
		lightTraceContribution = 0;

	if (gLightTraceOnly)
		gRadiance[index.xy] = float4(lightTraceContribution, 1);
	else if (any(lightTraceContribution > 0))
		gRadiance[index.xy] += float4(lightTraceContribution, 0);

}