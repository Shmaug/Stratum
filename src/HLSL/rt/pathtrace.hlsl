#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E primary
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E indirect

#include "rtscene.hlsli"

#define gImageCount 64

[[vk::constant_id(0)]] const uint gMaxBounces = 2;
[[vk::constant_id(1)]] const uint gSamplingFlags = 3;
[[vk::constant_id(2)]] const uint gEnvironmentMap = -1;
[[vk::constant_id(3)]] const bool gDemodulateAlbedo = false;

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

[[vk::binding(17)]] SamplerState gSampler;
[[vk::binding(18)]] Texture2D<float2> gEnvironmentConditionalDistribution;
[[vk::binding(19)]] Texture2D<float2> gEnvironmentMarginalDistribution;
[[vk::binding(20)]] Texture2D<float4> gImages[gImageCount];

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

float next_rng_sample(inout uint4 v) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
	v = v ^ (v >> 16u);
	v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
	return float(v.x) / float(0xffffffffu);
}

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
	if (i < gImageCount) {
		float4 a = gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, texcoord.xy, texcoord.z);
		md.mAlbedo *= a.rgb;
		md.mTransmission *= 1 - a.w; 
	}
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
float3 sample_environment(float r1, float r2, out float3 omega_in, out float pdf) {
	uint2 hdrResolution;
	uint mips;
	gImages[gEnvironmentMap].GetDimensions(0, hdrResolution.x, hdrResolution.y, mips);

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
		switch (rayQuery.CandidateType()) {
			case CANDIDATE_PROCEDURAL_PRIMITIVE: {
				float2 st = ray_sphere(rayQuery.CandidateObjectRayOrigin(), rayQuery.CandidateObjectRayDirection(), 0, 1);
				if (st.x < st.y) {
					float t = st.x < 0 ? st.y : st.x;
					if (t <= rayQuery.CommittedRayT() && t >= rayQuery.RayTMin())
						rayQuery.CommitProceduralPrimitiveHit(t);
				}
				break;
			}
			case CANDIDATE_NON_OPAQUE_TRIANGLE: {
				SurfaceData sfc = surface_attributes(gInstances[rayQuery.CandidateInstanceIndex()], gVertices, gIndices, rayQuery.CandidatePrimitiveIndex(), rayQuery.CandidateTriangleBarycentrics());
				MaterialData material = sample_image_attributes(sfc);
				if (material.mTransmission > 0)
					rayQuery.CommitNonOpaqueTriangleHit();
				break;
			}
		}
	}
	return rayQuery.CommittedStatus() != COMMITTED_NOTHING;
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

	uint instanceIndex = -1;
	uint primitiveIndex = 0;
	float2 bary = 0;
	float3 normal = 0;
	float3 z = float3(1.#INF, 0, 0);
	float2 prevUV = uv;
	RayQuery<RAY_FLAG_NONE> rayQuery;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
	if (do_ray_query(rayQuery)) {
		instanceIndex = rayQuery.CommittedInstanceID();
 		InstanceData instance = gInstances[instanceIndex];
		float3 pos = 0;
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// light
			primitiveIndex = -1;
			bary = 0;
			pos = ray.Origin + ray.Direction*rayQuery.CommittedRayT();
			normal = 0;
		} else {
			primitiveIndex = rayQuery.CommittedPrimitiveIndex();
			bary = rayQuery.CommittedTriangleBarycentrics();
			SurfaceData sfc = surface_attributes(instance, gVertices, gIndices, primitiveIndex, bary);
			pos = sfc.v.mPositionU.xyz;
			normal = sfc.v.mNormalV.xyz;
		}

		float3 prevCamPos = transform_point(gWorldToPrevCamera, pos);
		float3 screenNormal = normalize(transform_vector(inverse(gCameraToWorld), normal));
		z = float3(rayQuery.CommittedRayT(), 1/abs(screenNormal.z), length(prevCamPos));
		
		float4 prevScreenPos = project_point(gPrevProjection, prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevUV = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	} else 
		bary = float2(atan2(ray.Direction.z, ray.Direction.x)/M_PI *.5 + .5, acos(clamp(ray.Direction.y, -1, 1)) / M_PI);

	gVisibility[index.xy] = uint4(instanceIndex, primitiveIndex, asuint(bary));
	gNormal[index.xy] = float4(normal, 1);
	gZ[index.xy] = float4(z,0);
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
		uint4 rng = gRNGSeed[index.xy];
		if (all(rng == 0)) {
			rng = uint4(index.xy, gPushConstants.gRandomSeed, index.x + index.y);
			gRNGSeed[index.xy] = rng;
		}
		
		float3 throughput = 1;
		float pdf_bsdf = 1;
		float3 absorption = 0;
		float hit_t = 0;

		RayQuery<RAY_FLAG_NONE> rayQuery;
		
		RayDesc ray;
		for (uint bounceIndex = 0; bounceIndex < gMaxBounces; bounceIndex++) {
			InstanceData instance = gInstances[vis.x];

			if (vis.y == -1) { // light primitive
				LightData light = gLights[instance.mIndexStride>>8];
				PackedLightData p;
				p.v = light.mPackedData;
				float pdf_light = hit_t*hit_t / (2*M_PI*p.radius()*p.radius()) / (float)gPushConstants.gLightCount;
				float w = 1;
				if ((gSamplingFlags&SAMPLE_FLAG_BG_IS) && bounceIndex > 0) w *= powerHeuristic(pdf_bsdf, pdf_light);
				radiance += w * throughput * light.mEmission;
				break;
			}

			SurfaceData sfc = surface_attributes(instance, gVertices, gIndices, vis.y, asfloat(vis.zw));
			MaterialData material = sample_image_attributes(sfc);
			
			bool front_face = true;
			if (dot(ray.Direction, sfc.Ng) > 0) {
				sfc.v.mNormalV.xyz = -sfc.v.mNormalV.xyz;
				sfc.Ng = -sfc.Ng;
				front_face = false;
			}

			if (bounceIndex == 0) {
				ray.Origin = gCameraToWorld.mTranslation;
				ray.Direction = sfc.v.mPositionU.xyz - ray.Origin;
				hit_t = length(ray.Direction);
				ray.Direction /= hit_t;
				ray.TMin = gProjection.mNear;
				ray.TMax = 1.#INF;
				albedo = material.mAlbedo;
				if (!front_face) absorption = material.mAbsorption;
			}
			
			throughput *= exp(-absorption * hit_t);
			
			if (any(material.mEmission > 0) && front_face) {
				LightData light = gLights[instance.mIndexStride>>8];
				PackedLightData p;
				p.v = light.mPackedData;
				float pdf_light = hit_t*hit_t / (dot(-ray.Direction, sfc.Ng) * sfc.area) / p.prim_count() / (float)gPushConstants.gLightCount;
				if ((gSamplingFlags&SAMPLE_FLAG_LIGHT_IS) && bounceIndex > 0) throughput *= powerHeuristic(pdf_bsdf, pdf_light);
				radiance += throughput * material.mEmission;
			}
			
			DisneyMaterial disneyMat;
			disneyMat.albedo = material.mAlbedo;
			disneyMat.specular = 0.5;
			disneyMat.metallic = material.mMetallic;
			disneyMat.roughness = material.mRoughness*material.mRoughness;
			disneyMat.subsurface = 0;
			disneyMat.specularTint = 0;
			disneyMat.sheen = 0;
			disneyMat.sheenTint = 0.5;
			disneyMat.clearcoat = 0;
			disneyMat.clearcoatGloss = 0.03;
			disneyMat.specTrans = material.mTransmission;
			disneyMat.eta = front_face ? material.mIndexOfRefraction : 1/material.mIndexOfRefraction;

			if (disneyMat.metallic < 1 || disneyMat.roughness > MIN_ROUGHNESS) { // no MIS for delta specular
				RayDesc shadowRay;
				shadowRay.Origin = ray_offset(sfc.v.mPositionU.xyz, sfc.Ng);
				shadowRay.TMin = 0;
				
				if (gSamplingFlags & SAMPLE_FLAG_BG_IS && gEnvironmentMap < gImageCount) {
					float3 omega_in;
					float pdf_light;
					float3 bg = sample_environment(next_rng_sample(rng), next_rng_sample(rng), omega_in, pdf_light);
					if (pdf_light > 0) {
						shadowRay.Direction = omega_in;
						shadowRay.TMax = 1.#INF;
						rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
						if (!do_ray_query(rayQuery)) {
							float pdf_bsdf_mis;
							float3 f = DisneyEval(disneyMat, -ray.Direction, sfc.v.mNormalV.xyz, omega_in, pdf_bsdf_mis);
							if (pdf_bsdf_mis > 0) {
								bg *= powerHeuristic(pdf_light, pdf_bsdf_mis);
								radiance += throughput * f * abs(dot(omega_in, sfc.v.mNormalV.xyz)) * bg / pdf_light;
							}
						}
					}
				}

				if (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) {
					uint lightIndex = min(uint(next_rng_sample(rng) * gPushConstants.gLightCount), gPushConstants.gLightCount-1);
					uint primIndex;
					LightData light = gLights[lightIndex];
					PackedLightData p;
					p.v = light.mPackedData;

					float3 light_emission = light.mEmission;
					float pdf_light = 1/(float)gPushConstants.gLightCount;

					switch (light.mType) {
						case LIGHT_TYPE_DISTANT: {
							shadowRay.TMax = 1.#INF;
							shadowRay.Direction = -transform_vector(light.mLightToWorld, float3(0,0,sign(light.mShadowProjection.mNear)));
							if (p.radius()) {
								float3 T, B;
								Onb(shadowRay.Direction, T, B);
								float2 u = p.radius()*ConcentricSampleDisk(next_rng_sample(rng), next_rng_sample(rng));
								shadowRay.Direction = T*u.x + B*u.y + shadowRay.Direction*sqrt(1 - dot(u,u));
							}
							if (dot(shadowRay.Direction, sfc.v.mNormalV.xyz) <= 0)
								pdf_light = 0;
							break;
						}

						case LIGHT_TYPE_SPOT:
						case LIGHT_TYPE_POINT: {
							float3 L = normalize(light.mLightToWorld.mTranslation - shadowRay.Origin);
							float3 T, B;
							Onb(L, T, B);
							float3 s = p.radius()*UniformSampleHemisphere(next_rng_sample(rng), next_rng_sample(rng));
							shadowRay.Direction = (light.mLightToWorld.mTranslation + (T*s.x + B*s.y + L*s.z)) - shadowRay.Origin;
							float distSq = dot(shadowRay.Direction, shadowRay.Direction);
							float dist = sqrt(distSq);
							shadowRay.Direction /= dist;
							if (dot(shadowRay.Direction, sfc.v.mNormalV.xyz) > 0) {
								// TODO: spot light attenuation
								shadowRay.TMax = dist - p.radius();
								pdf_light *= distSq / (2*M_PI*p.radius()*p.radius());
							} else 
								pdf_light = 0;
							break;
						}

						case LIGHT_TYPE_EMISSIVE_MATERIAL: {				
							float2 bary = float2(next_rng_sample(rng), next_rng_sample(rng));
							if (bary.x + bary.y >= 1) bary = 1 - bary;
							primIndex = min(next_rng_sample(rng)*p.prim_count(), p.prim_count() - 1);
							SurfaceData lsfc = surface_attributes(gInstances[p.instance_index()], gVertices, gIndices, primIndex, bary);
							MaterialData lmaterial = sample_image_attributes(lsfc);
							
							shadowRay.Direction = lsfc.v.mPositionU.xyz - shadowRay.Origin;
							float distSq = dot(shadowRay.Direction, shadowRay.Direction);
							float dist = sqrt(distSq);
							shadowRay.Direction /= dist;
							shadowRay.TMax = dist - 2e-5;
							float lnv = dot(shadowRay.Direction, -lsfc.v.mNormalV.xyz);
							if (dot(shadowRay.Direction, sfc.v.mNormalV.xyz) > 0 && lnv > 0 && lsfc.area > 0) {
								light_emission = lmaterial.mEmission;
								pdf_light *= distSq / (lnv * lsfc.area) / p.prim_count();
							} else
								pdf_light = 0;
							break;
						}
					}

					if (pdf_light > 0) {
						rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
						if (!do_ray_query(rayQuery) || (rayQuery.CommittedInstanceIndex() == lightIndex && (light.mType != LIGHT_TYPE_EMISSIVE_MATERIAL || rayQuery.CommittedPrimitiveIndex() == primIndex))) {
							float pdf_bsdf_mis;
							float3 f = DisneyEval(disneyMat, -ray.Direction, sfc.v.mNormalV.xyz, shadowRay.Direction, pdf_bsdf_mis);
							if (pdf_bsdf_mis > 0) {
								if (light.mType != LIGHT_TYPE_DISTANT) light_emission *= powerHeuristic(pdf_light, pdf_bsdf_mis);
								radiance += throughput * f * dot(sfc.v.mNormalV.xyz, shadowRay.Direction) * light_emission / pdf_light;
							}
						}
					}
				}
			}

			float3 omega_in;
			uint flag;
			float3 f = DisneySample(rng, disneyMat, -ray.Direction, sfc.v.mNormalV.xyz, sfc.v.mTangent.xyz, omega_in, pdf_bsdf, flag);
			if (pdf_bsdf == 0) break;
			
			// setup next bounce

			throughput *= f * abs(dot(sfc.v.mNormalV.xyz, omega_in)) / pdf_bsdf;
			if (all(throughput < 1e-6)) break;

			if (dot(ray.Direction, sfc.Ng) < 0) absorption = material.mAbsorption;
			else absorption = 0;

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
					if (gSamplingFlags & SAMPLE_FLAG_BG_IS) bg *= powerHeuristic(pdf_bsdf, pdf_bg);				
					radiance += throughput * bg;
				}
				break;
			}
			vis.x = rayQuery.CommittedInstanceIndex();
			if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT)
				vis.y = -1;
			else {
				vis.y = rayQuery.CommittedPrimitiveIndex();
				vis.zw = asuint(rayQuery.CommittedTriangleBarycentrics());
			}
			hit_t = rayQuery.CommittedRayT();

			//if (bounceIndex >= gRussianRouletteDepth) {
			//	float q = min(max3(throughput) + 0.001, 0.95);
			//	if (rng.next_sample() > q) break;
			//	throughput /= q;
			//}
		}
	}

	if (gDemodulateAlbedo) {
		if (albedo.x > 0) radiance.x /= albedo.x;
		if (albedo.y > 0) radiance.y /= albedo.y;
		if (albedo.z > 0) radiance.z /= albedo.z;
	}

	gRadiance[index.xy] = float4(radiance, 1);
	gAlbedo[index.xy] = float4(albedo, 1);
}