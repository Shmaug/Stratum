#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E primary
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E indirect

#include "rtscene.hlsli"

#define gImageCount 64

[[vk::constant_id(0)]] const uint gMaxBounces = 3;
[[vk::constant_id(1)]] const uint gRussianRouletteDepth = 1;
[[vk::constant_id(2)]] const uint gSamplingFlags = 3;
[[vk::constant_id(3)]] const uint gEnvironmentMap = -1;
[[vk::constant_id(4)]] const bool gDemodulateAlbedo = false;

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] StructuredBuffer<VertexData> gVertices;
[[vk::binding(2)]] ByteAddressBuffer gIndices;
[[vk::binding(3)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(4)]] StructuredBuffer<MaterialData> gMaterials;
[[vk::binding(5)]] StructuredBuffer<LightData> gLights;

[[vk::binding(6)]] RWTexture2D<uint4> gVisibility;

[[vk::binding(7)]] RWTexture2D<float4> gRadiance;
[[vk::binding(8)]] RWTexture2D<float4> gAlbedo;
[[vk::binding(9)]] RWTexture2D<float4> gNormal;
[[vk::binding(10)]] RWTexture2D<float4> gZ;
[[vk::binding(11)]] RWTexture2D<uint4> gRNGSeed;
[[vk::binding(12)]] RWTexture2D<float2> gPrevUV;

[[vk::binding(13)]] SamplerState gSampler;
[[vk::binding(14)]] Texture2D<float2> gEnvironmentConditionalDistribution;
[[vk::binding(15)]] Texture2D<float2> gEnvironmentMarginalDistribution;
[[vk::binding(16)]] Texture2D<float4> gImages[gImageCount];

cbuffer gCameraData {
	TransformData gCameraToWorld;
	ProjectionData gProjection;
	TransformData gWorldToPrevCamera;
	ProjectionData gPrevProjection;
};

[[vk::push_constant]] const struct {
	uint gLightCount;
	uint gRandomSeed;
	float gExposure;
	float gGamma;
} gPushConstants;

class RandomSampler {
	uint4 v;

	float next_sample() {
		v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    v = v ^ (v >> 16u);
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
		return float(v.x) / float(0xffffffffu);
	}
};

#include "disney.hlsli"
#include "a-svgf/svgf_shared.hlsli"
#include "../ACES.hlsli"

MaterialData sample_image_attributes(inout SurfaceData sfc) {
	// sample textures
	float3 texcoord = float3(sfc.v.mPositionU.w, sfc.v.mNormalV.w, 0);
	MaterialData md = gMaterials[sfc.instance.mMaterialIndex];
	ImageIndices inds;
	inds.v = md.mImageIndices;
	uint i = inds.albedo();
	if (i < gImageCount) md.mAlbedo *= gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z).rgb;
	i = inds.metallic();
	if (i < gImageCount)  md.mMetallic = saturate(md.mMetallic*gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z)[inds.metallic_channel()]);
	i = inds.roughness();
	if (i < gImageCount) md.mRoughness = saturate(md.mRoughness*gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z)[inds.roughness_channel()]);
	i = inds.emission();
	if (i < gImageCount)  md.mEmission *= gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z).rgb;
	i = inds.normal();
	if (i < gImageCount) {
		float3 bump = gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z).xyz*2 - 1;
		bump.xy *= md.mNormalScale;
		sfc.v.mNormalV.xyz = normalize(bump.x*sfc.v.mTangent.xyz + bump.y*cross(sfc.v.mTangent.xyz, sfc.v.mNormalV.xyz) + bump.z*sfc.v.mNormalV.xyz);
	}
	// ortho-normalize
	sfc.v.mTangent.xyz = normalize(sfc.v.mTangent.xyz - sfc.v.mNormalV.xyz * dot(sfc.v.mNormalV.xyz, sfc.v.mTangent.xyz));
	return md;
}

float environment_pdf(float3 omega_in, float2 uv) {
	uint2 hdrResolution;
	uint mips;
	gImages[gEnvironmentMap].GetDimensions(0, hdrResolution.x, hdrResolution.y, mips);
	float pdf = gEnvironmentConditionalDistribution.SampleLevel(gSampler, uv, 0).y * gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(uv.y, 0), 0).y;
	float sinTheta = sqrt(1 - omega_in.y*omega_in.y);
	return (pdf * hdrResolution.x*hdrResolution.y) / (2 * M_PI * M_PI * sinTheta);
}
float3 sample_environment(inout RandomSampler rng, out float3 omega_in, out float pdf) {
	uint2 hdrResolution;
	uint mips;
	gImages[gEnvironmentMap].GetDimensions(0, hdrResolution.x, hdrResolution.y, mips);

	float r1 = rng.next_sample();
	float r2 = rng.next_sample();
	float v = gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(r1, 0), 0).x;
	float u = gEnvironmentConditionalDistribution.SampleLevel(gSampler, float2(r2, v), 0).x;

	pdf = gEnvironmentConditionalDistribution.SampleLevel(gSampler, float2(u, v), 0).y * gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(v, 0), 0).y;

	float phi = u * 2*M_PI;
	float theta = v * M_PI;

	float sinTheta = sin(theta);
	if (sinTheta == 0) pdf = 0;
	else pdf *= hdrResolution.x*hdrResolution.y / (2 * M_PI * M_PI * sinTheta);
	omega_in = float3(-sinTheta * cos(phi), cos(theta), -sinTheta * sin(phi));
	return gImages[gEnvironmentMap].SampleLevel(gSampler, float2(u, v), 0).rgb;
}

float3 ray_offset(float3 P, float3 Ng) {
  const float epsilon_f = 1e-5f;
  /* ideally this should match epsilon_f, but instancing and motion blur
   * precision makes it problematic */
  const float epsilon_test = 1.0f;
  const int epsilon_i = 32;

  //const float epsilon_f = 1e-4f;
  //return P + epsilon_f * Ng;

  float3 res;

  /* x component */
  if (abs(P.x) < epsilon_test) {
    res.x = P.x + Ng.x * epsilon_f;
  } else {
    uint ix = asuint(P.x);
    ix += ((ix ^ asuint(Ng.x)) >> 31) ? -epsilon_i : epsilon_i;
    res.x = asfloat(ix);
  }

  /* y component */
  if (abs(P.y) < epsilon_test) {
    res.y = P.y + Ng.y * epsilon_f;
  } else {
    uint iy = asuint(P.y);
    iy += ((iy ^ asuint(Ng.y)) >> 31) ? -epsilon_i : epsilon_i;
    res.y = asfloat(iy);
  }

  /* z component */
  if (abs(P.z) < epsilon_test) {
    res.z = P.z + Ng.z * epsilon_f;
  } else {
    uint iz = asuint(P.z);
    iz += ((iz ^ asuint(Ng.z)) >> 31) ? -epsilon_i : epsilon_i;
    res.z = asfloat(iz);
  }

  return res;


}

bool do_ray_query(inout RayQuery<RAY_FLAG_NONE> rayQuery) {
	while (rayQuery.Proceed()) {
		switch(rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE:
				break;
			case CANDIDATE_NON_OPAQUE_TRIANGLE: {
				//MaterialData material = gMaterials[rayQuery.CandidateInstanceIndex()];
				//rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}
	return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

float3 direct_light(inout RayQuery<RAY_FLAG_NONE> rayQuery, inout RandomSampler rng, float3 V, SurfaceData sfc, DisneyMaterial mat) {
	float3 Li = 0;
	RayDesc shadowRay;
	shadowRay.Origin = ray_offset(sfc.v.mPositionU.xyz, sfc.Ng);
	shadowRay.TMin = 0;
	
	if (gSamplingFlags & SAMPLE_FLAG_BG_IS && gEnvironmentMap < gImageCount) {
		float3 omega_in;
		float pdf_light;
		float3 bg = sample_environment(rng, omega_in, pdf_light);
		if (pdf_light > 0) {
			shadowRay.Direction = omega_in;
			shadowRay.TMax = 1.#INF;
			rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
			if (!do_ray_query(rayQuery)) {
				float pdf_bsdf;
				float3 f = DisneyEval(mat, V, sfc.v.mNormalV.xyz, omega_in, pdf_bsdf);
				if (pdf_bsdf > 0) {
					bg *= powerHeuristic(pdf_light, pdf_bsdf);
					Li += f * abs(dot(omega_in, sfc.v.mNormalV.xyz)) * bg / pdf_light;
				}
			}
		}
	}

	/*
	{
		LightData light = gLights[min(uint(rng.next_sample() * gPushConstants.gLightCount), gPushConstants.gLightCount-1)];

		float pdf_light = 1/(float)gPushConstants.gLightCount;

		switch (light.mType) {
			case LIGHT_TYPE_DISTANT:
				shadowRay.Direction = transform_vector(light.mLightToWorld, float3(0,0,sign(light.mShadowProjection.mNear)));
				shadowRay.TMax = 1.#INF;
				break;

			case LIGHT_TYPE_SPOT:
			case LIGHT_TYPE_POINT: {
				float r1 = rng.next_sample();
				float r2 = rng.next_sample();

				float3 sphereCentertoSurface = shadowRay.Origin - light.mLightToWorld.mTranslation;
				float distToSphereCenter = length(sphereCentertoSurface);
				
				// assumes the light will be hit only from the outside
				sphereCentertoSurface /= distToSphereCenter;
				float3 sampledDir = UniformSampleHemisphere(r1, r2);
				float3 T, B;
				Onb(sphereCentertoSurface, T, B);
				sampledDir = T * sampledDir.x + B * sampledDir.y + sphereCentertoSurface * sampledDir.z;

				float3 lightSurfacePos = light.mLightToWorld.mTranslation + sampledDir * asfloat(light.mShadowIndex);

				shadowRay.Direction = lightSurfacePos - shadowRay.Origin;
				float dist = length(shadowRay.Direction);
				float distSq = dist * dist;

				shadowRay.Direction /= dist;
				float3 lightNormal = normalize(lightSurfacePos - light.mLightToWorld.mTranslation);
				pdf_light *= distSq / (light.area * 0.5 * abs(dot(lightNormal, shadowRay.Direction)));
				break;
			}

			case LIGHT_TYPE_EMISSIVE_MATERIAL: {
				uint primCount = asuint(light.mCosOuterAngle);
				uint prim = asuint(light.mCosInnerAngle) + min(rng.next_sample()*primCount, primCount - 1);
				
				float2 bary = float2(rng.next_sample(), rng.next_sample());
				if (bary.x + bary.y >= 1) bary = 1 - bary;
				
				SurfaceData sd = surface_attributes(light.mShadowIndex, prim, bary);
				shadowRay.Direction = sd.v.mPositionU.xyz;
				
				shadowRay.Direction = sd.v.mPositionU.xyz - shadowRay.Origin;
				shadowRay.TMax = length(shadowRay.Direction);
				shadowRay.Direction /= shadowRay.TMax;

				float lnv = dot(shadowRay.Direction, -sd.Ng);
				if (lnv <= 0 || sd.area <= 1e-4) {
					pdf_light = 0;
					break;
				}
				
				pdf_light *= shadowRay.TMax*shadowRay.TMax / (lnv * sd.area);
				pdf_light *= 1/primCount;
				shadowRay.TMax -= 1e-4;
				break;
			}
		}

		if (pdf_light > 0) {
			rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
			if (!do_ray_query(rayQuery)) {
				float pdf_bsdf;
				float3 f = DisneyEval(mat, omega_out, sfc.v.mNormalV.xyz, shadowRay.Direction, pdf_bsdf);

				float weight = 1;
				if (light.mType != LIGHT_TYPE_DISTANT) // No MIS for distant light
					weight = powerHeuristic(pdf_light, pdf_bsdf);

				if (pdf_bsdf > 0)
					Li += weight * f * abs(dot(sfc.v.mNormalV.xyz, shadowRay.Direction)) * light.mEmission / pdf_light;
			}
		}
	}
	*/

	return Li;
}

[numthreads(8,8,1)]
void primary(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gVisibility.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float2 uv = (index.xy + 0.5)/float2(resolution);
	float3 screenPos = float3(2*uv - 1, 1);
	screenPos.y = -screenPos.y;
	float3 unprojected = back_project(gProjection, screenPos);
	RayDesc ray;
	ray.Direction = normalize(transform_vector(gCameraToWorld, float3(unprojected.xy, sign(gProjection.mNear))));
	ray.Origin = gCameraToWorld.mTranslation;
	ray.TMin = gProjection.mNear;
	ray.TMax = 1.#INF;

	float2 bary = 0;
	uint instanceIndex = -1;
	uint primitiveIndex = -1;
	float3 normal = 0;
	float4 z = float4(1.#INF, 0, 0, 0);
	float2 prevUV = uv;
	RayQuery<RAY_FLAG_NONE> rayQuery;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
	if (do_ray_query(rayQuery)) {
		bary = rayQuery.CommittedTriangleBarycentrics();
		instanceIndex = rayQuery.CommittedInstanceID();
		primitiveIndex = rayQuery.CommittedPrimitiveIndex();
 		InstanceData instance = gInstances[instanceIndex];
		SurfaceData sfc = surface_attributes(instance, gVertices, gIndices, primitiveIndex, bary);
		normal = sfc.v.mNormalV.xyz;

		float3 prevCamPos = transform_point(gWorldToPrevCamera, sfc.v.mPositionU.xyz);
		float3 screenNormal = transform_vector(inverse(gCameraToWorld), normal);
		z = float4(rayQuery.CommittedRayT(), 1/abs(screenNormal.z), length(prevCamPos), 0);
		
		float4 prevScreenPos = project_point(gPrevProjection, prevCamPos);
		prevUV = prevScreenPos.xy /= prevScreenPos.w;
		prevUV.y = -prevUV.y;
		prevUV = prevUV*.5 + .5;
	} else {
		float theta = acos(clamp(ray.Direction.y, -1, 1));
		bary = float2(atan2(ray.Direction.z, ray.Direction.x)/M_PI *.5 + .5, theta / M_PI);
	}

	gVisibility[index.xy] = uint4(instanceIndex, primitiveIndex, asuint(bary));
	gNormal[index.xy] = float4(normal, 1);
	gZ[index.xy] = z;
	gPrevUV[index.xy] = prevUV;
}

[numthreads(8,8,1)]
void indirect(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gRadiance.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	uint4 vis = gVisibility[index.xy];
	
	float3 radiance = 0;
	float3 albedo = 1;
	if (vis.x == -1) {
		if (gEnvironmentMap < gImageCount)
			radiance = gImages[gEnvironmentMap].SampleLevel(gSampler, asfloat(vis.zw), 0).rgb;
	} else {
		uint4 rngSeed = gRNGSeed[index.xy];
		if (all(rngSeed == 0)) {
			rngSeed = uint4(index.xy, gPushConstants.gRandomSeed, index.x + index.y);
			gRNGSeed[index.xy] = rngSeed;
		}
		
		RandomSampler rng;
		rng.v = rngSeed;
		
		float3 throughput = 1;
		float pdf_bsdf = 1;

		RayQuery<RAY_FLAG_NONE> rayQuery;
		
		RayDesc ray;
		for (uint bounceIndex = 0; bounceIndex < gMaxBounces && any(throughput > 1e-6) && pdf_bsdf > 0; bounceIndex++) {
			SurfaceData sfc = surface_attributes(gInstances[vis.x], gVertices, gIndices, vis.y, asfloat(vis.zw));
			MaterialData material = sample_image_attributes(sfc);

			if (bounceIndex == 0) {
				ray.Origin = gCameraToWorld.mTranslation;
				ray.Direction = normalize(sfc.v.mPositionU.xyz - ray.Origin);
				ray.TMin = gProjection.mNear;
				ray.TMax = 1.#INF;
				albedo = material.mAlbedo;
			}
			
			radiance += throughput * material.mEmission;

			//if (gSamplingFlags & SAMPLE_FLAG_IS && any(material.mEmission > 0)) {
			//	float pdf_light;
			//	float mis = (bounceIndex == 0) ? 1 : powerHeuristic(pdf_bsdf, pdf_light);
			//	radiance += throughput * material.mEmission * mis;
			//	break;
			//}
			DisneyMaterial disneyMat;
			disneyMat.albedo = material.mAlbedo;
			disneyMat.specular = 0.5;
			disneyMat.metallic = material.mMetallic;
			disneyMat.roughness = material.mRoughness;
			disneyMat.subsurface = 0;
			disneyMat.specularTint = 0;
			disneyMat.sheen = 0;
			disneyMat.sheenTint = 0.5;
			disneyMat.clearcoat = 0;
			disneyMat.clearcoatGloss = 0.03;
			disneyMat.specTrans = material.mTransmission;
			disneyMat.eta = material.mIndexOfRefraction;

			if (dot(ray.Direction, sfc.Ng) > 0) {
				throughput *= exp(-material.mAbsorption * length(ray.Origin - sfc.v.mPositionU.xyz));
				sfc.v.mNormalV.xyz = -sfc.v.mNormalV.xyz;
				sfc.Ng = -sfc.Ng;
				disneyMat.eta = 1/disneyMat.eta;
			}

			if (gSamplingFlags & SAMPLE_FLAG_IS) // && !bsdf.deltaSpecular)
				radiance += throughput * direct_light(rayQuery, rng, -ray.Direction, sfc, disneyMat);

			float3 omega_in;
			float3 f = DisneySample(rng, disneyMat, -ray.Direction, sfc.v.mNormalV.xyz, sfc.v.mTangent.xyz, omega_in, pdf_bsdf);
			if (pdf_bsdf > 0) throughput *= f * abs(dot(sfc.v.mNormalV.xyz, omega_in)) / pdf_bsdf;

			ray.Direction = omega_in;
			ray.Origin = ray_offset(sfc.v.mPositionU.xyz, dot(omega_in, sfc.Ng) < 0 ? -sfc.Ng : sfc.Ng);
			ray.TMin = 0;
			ray.TMax = 1.#INF;

			rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
			if (!do_ray_query(rayQuery)) {
				if (gEnvironmentMap < gImageCount) {
					float theta = acos(clamp(ray.Direction.y, -1, 1));
					float2 uv = float2(atan2(ray.Direction.z, ray.Direction.x)/M_PI *.5 + .5, theta / M_PI);
					float pdf_bg = environment_pdf(ray.Direction, uv);
					float3 bg = gImages[gEnvironmentMap].SampleLevel(gSampler, uv, 0).rgb;
					if ((gSamplingFlags & SAMPLE_FLAG_IS) && (gSamplingFlags & SAMPLE_FLAG_BG_IS))
						bg *= powerHeuristic(pdf_bsdf, pdf_bg);				
					radiance += throughput * bg;
				}
				break;
			}

			if (bounceIndex >= gRussianRouletteDepth) {
				float q = min(max3(throughput) + 0.001, 0.95);
				if (rng.next_sample() > q) break;
				throughput /= q;
			}

			vis = uint4(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedPrimitiveIndex(), asuint(rayQuery.CommittedTriangleBarycentrics()));
		}
	}

	if (gDemodulateAlbedo) radiance /= albedo;

	gRadiance[index.xy] = float4(radiance, 1);
	gAlbedo[index.xy] = float4(albedo, 1);
}