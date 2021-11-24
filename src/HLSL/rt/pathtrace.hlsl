#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E primary
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E indirect

#include "rtscene.hlsli"

#define gImageCount 1024

[[vk::constant_id(0)]] const uint gMaxBounces = 3;
[[vk::constant_id(1)]] const uint gSamplingFlags = 0xF;
[[vk::constant_id(2)]] const uint gEnvironmentMap = -1;
[[vk::constant_id(3)]] const bool gDemodulateAlbedo = false;

[[vk::binding(0)]] RaytracingAccelerationStructure gScene;
[[vk::binding(1)]] StructuredBuffer<VertexData> gVertices;
[[vk::binding(2)]] ByteAddressBuffer gIndices;
[[vk::binding(3)]] StructuredBuffer<InstanceData> gInstances;
[[vk::binding(4)]] StructuredBuffer<MaterialData> gMaterials;
[[vk::binding(5)]] StructuredBuffer<LightData> gLights;

[[vk::binding(6)]] RWTexture2D<uint4> gRNGSeed;
[[vk::binding(7)]] RWTexture2D<uint4> gVisibility;
[[vk::binding(8)]] RWTexture2D<float4> gNormal;
[[vk::binding(9)]] RWTexture2D<float4> gZ;
[[vk::binding(10)]] RWTexture2D<float2> gPrevUV;
[[vk::binding(11)]] RWTexture2D<float2> gReservoirs;
[[vk::binding(12)]] RWTexture2D<float4> gReservoirRNG;

[[vk::binding(13)]] RWTexture2D<float4> gRadiance;
[[vk::binding(14)]] RWTexture2D<float4> gAlbedo;

[[vk::binding(17)]] SamplerState gSampler;
[[vk::binding(18)]] Texture2D<float2> gEnvironmentConditionalDistribution;
[[vk::binding(19)]] Texture2D<float2> gEnvironmentMarginalDistribution;
[[vk::binding(20)]] Texture2D<float4> gImages[gImageCount];

cbuffer gCameraData {
	TransformData gCameraToWorld;
	TransformData gWorldToCamera;
	ProjectionData gProjection;
	TransformData gWorldToPrevCamera;
	ProjectionData gPrevProjection;
};

[[vk::push_constant]] const struct {
	uint gLightCount;
	uint gRandomSeed;
	float gEnvironmentMapGamma;
	float gEnvironmentMapExposure;
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

inline DisneyBSDF to_disney_bsdf(MaterialData material) {
	DisneyBSDF bsdf;
	bsdf.albedo = material.mAlbedo;
	bsdf.specular = 0.5;
	bsdf.metallic = material.mMetallic;
	bsdf.roughness = material.mRoughness*material.mRoughness;
	bsdf.subsurface = 0;
	bsdf.specularTint = 0;
	bsdf.sheen = 0;
	bsdf.sheenTint = 0.5;
	bsdf.clearcoat = 0;
	bsdf.clearcoatGloss = 0.03;
	bsdf.specTrans = material.mTransmission;
	bsdf.eta = material.mIndexOfRefraction;
	return bsdf;
}

struct LightSample {
	float3 radiance;
	float pdf;
	float3 omega_in;
	float dist;
	bool punctual;
};
class Reservoir {
	uint4 z_rng;
	uint4 z_vis;
	float w_sum;
	uint M;

	inline bool update(float r1, uint4 s_rng, uint4 s_vis, float w) {
		w_sum += w;
		M++;
		if (r1 < w/w_sum) {
			z_rng = s_rng;
			z_vis = s_vis;
			return true;
		}
		return false;
	}
};

float4 sample_image(Texture2D<float4> img, SurfaceSample sfc) {
	//return img.SampleLevel(gSampler, float2(sfc.v.mU, sfc.v.mV), 0);
	return img.SampleGrad(gSampler, float2(sfc.v.mU, sfc.v.mV), sfc.dUV.dx, sfc.dUV.dy);
}
MaterialData sample_image_attributes(InstanceData instance, inout SurfaceSample sfc) {
	MaterialData md = gMaterials[instance.mMaterialIndex];
	ImageIndices inds;
	inds.v = md.mImageIndices;
	uint i = inds.albedo();
	if (i < gImageCount) {
		float4 a = sample_image(gImages[NonUniformResourceIndex(i)], sfc);
		md.mAlbedo *= a.rgb;
		md.mTransmission = 1 - a.w*(1 - md.mTransmission); 
	}
	i = inds.metallic();
	if (i < gImageCount) md.mMetallic = saturate(md.mMetallic*sample_image(gImages[NonUniformResourceIndex(i)], sfc)[inds.metallic_channel()]);
	i = inds.roughness();
	if (i < gImageCount) md.mRoughness = saturate(md.mRoughness*sample_image(gImages[NonUniformResourceIndex(i)], sfc)[inds.roughness_channel()]);
	i = inds.emission();
	if (i < gImageCount) md.mEmission *= sample_image(gImages[NonUniformResourceIndex(i)], sfc).rgb;
	i = inds.normal();
	if (i < gImageCount) {
		float3 bump = sample_image(gImages[NonUniformResourceIndex(i)], sfc).xyz*2 - 1;
		bump.xy *= md.mNormalScale;
		sfc.v.mNormal = normalize(bump.x*sfc.v.mTangent.xyz + bump.y*cross(sfc.v.mTangent.xyz, sfc.v.mNormal)*sfc.v.mTangent.w + sfc.v.mNormal);
	}
	// ortho-normalize
	sfc.v.mTangent.xyz = normalize(sfc.v.mTangent.xyz - sfc.v.mNormal * dot(sfc.v.mNormal, sfc.v.mTangent.xyz));
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
LightSample sample_environment(inout uint4 rng) {
	uint2 hdrResolution;
	uint mips;
	gImages[gEnvironmentMap].GetDimensions(0, hdrResolution.x, hdrResolution.y, mips);

	float2 uv;
	uv.y = gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(next_rng_sample(rng), 0), 0).x;
	uv.x = gEnvironmentConditionalDistribution.SampleLevel(gSampler, float2(next_rng_sample(rng), uv.y), 0).x;

	LightSample ls;
	ls.radiance = pow(gImages[gEnvironmentMap].SampleLevel(gSampler, uv, 0).rgb, 1/gPushConstants.gEnvironmentMapGamma)*gPushConstants.gEnvironmentMapExposure;
	ls.pdf = gEnvironmentConditionalDistribution.SampleLevel(gSampler, uv, 0).y * gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(uv.y, 0), 0).y;

	float phi = uv.x * 2*M_PI;
	float theta = uv.y * M_PI;
	float sinTheta = sin(theta);
	ls.omega_in = float3(-sinTheta * cos(phi), cos(theta), -sinTheta * sin(phi));
	ls.dist = 1.#INF;
	ls.pdf *= hdrResolution.x*hdrResolution.y/(2*M_PI*M_PI * sinTheta);
	ls.punctual = false;
	return ls;
}
LightSample sample_light(inout uint4 rng, float3 P, differential3 dP, differential3 dD, inout float pdf_pick) {
	uint lightIndex = min(uint(next_rng_sample(rng) * gPushConstants.gLightCount), gPushConstants.gLightCount-1);
	pdf_pick /= (float)gPushConstants.gLightCount;
	LightData light = gLights[lightIndex];
	PackedLightData p;
	p.v = light.mPackedData;
	
	LightSample ls;
	ls.radiance = light.mEmission;
	
	switch (light.mType) {
		case LIGHT_TYPE_DISTANT: {
			ls.punctual = true;
			ls.omega_in = transform_vector(light.mLightToWorld, float3(0,0,sign(light.mShadowProjection.mNear)));
			ls.pdf = 1;
			ls.dist = 1.#INF;
			if (p.radius() > 0) {
				float3 T, B;
				Onb(ls.omega_in, T, B);
				float2 u = p.radius()*ConcentricSampleDisk(next_rng_sample(rng), next_rng_sample(rng));
				ls.omega_in = T*u.x + B*u.y - ls.omega_in*sqrt(1 - dot(u,u));
			}
			break;
		}

		case LIGHT_TYPE_SPOT:
		case LIGHT_TYPE_POINT: {
			float3 light_pos = transform_point(light.mLightToWorld, 0);
			ls.omega_in = light_pos - P;
			float distSq = dot(ls.omega_in, ls.omega_in);
			ls.dist = sqrt(distSq);
			ls.omega_in /= ls.dist;
			if (p.radius() > 0) {
				float3 T, B;
				Onb(ls.omega_in, T, B);
				float3 s = p.radius()*UniformSampleHemisphere(next_rng_sample(rng), next_rng_sample(rng));
				light_pos += T*s.x + B*s.y - ls.omega_in*s.z;

				ls.omega_in = light_pos - P;
				distSq = dot(ls.omega_in, ls.omega_in);
				ls.dist = sqrt(distSq);
				ls.omega_in /= ls.dist;
				
				ls.pdf = distSq/(2*M_PI*p.radius()*p.radius());
				ls.punctual = false;
			} else {
				ls.pdf = distSq;
				ls.punctual = true;
			}
			break;
		}

		case LIGHT_TYPE_MESH: {
			ls.punctual = false;
			uint primIndex = min(next_rng_sample(rng)*p.prim_count(), p.prim_count() - 1);
			float2 bary = float2(next_rng_sample(rng), next_rng_sample(rng));
			if (dot(bary,1) > 1) bary = 1 - bary;
			InstanceData linstance = gInstances[p.instance_index()];
			SurfaceSample lsfc = surface_attributes(linstance, gVertices, gIndices, primIndex, bary, P, dP, dD);
			MaterialData lmaterial = gMaterials[linstance.mMaterialIndex];
			ImageIndices inds;
			inds.v = lmaterial.mImageIndices;
			uint ei = inds.emission();
			if (ei < gImageCount) ls.radiance *= sample_image(gImages[NonUniformResourceIndex(ei)], lsfc).rgb;
			
			ls.omega_in = lsfc.v.mPosition - P;
			float distSq = dot(ls.omega_in, ls.omega_in);
			ls.dist = sqrt(distSq);
			ls.omega_in /= ls.dist;
			float lnv = dot(lsfc.v.mNormal, -ls.omega_in);
			if (lnv > 0 && lsfc.area > 0) {
				ls.pdf = distSq / (lnv*lsfc.area);
				pdf_pick /= (float)p.prim_count();
			} else
				ls.pdf = 0;
			break;
		}
	}

	return ls;
}
LightSample get_light_sample(inout uint4 rng, float3 P, differential3 dP, differential3 dD, out float pdf_pick) {
	bool sample_bg = (gSamplingFlags & SAMPLE_FLAG_BG_IS) && gEnvironmentMap < gImageCount;
	bool sample_lights = (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

	pdf_pick = 1;
	if (sample_bg && sample_lights) {
		sample_bg = next_rng_sample(rng) < 0.5;
		sample_lights = !sample_bg;
		pdf_pick /= 2;
	}

	LightSample ls;
	if (sample_bg)
		ls = sample_environment(rng);
	else if (sample_lights)
		ls = sample_light(rng, P, dP, dD, pdf_pick);
	return ls;
}

RayDesc create_ray(float2 uv) {
	float3 screenPos = float3(2*uv - 1, 1);
	screenPos.y = -screenPos.y;

	RayDesc ray;
	ray.Direction = normalize(transform_vector(gCameraToWorld, float3(back_project(gProjection, screenPos).xy, sign(gProjection.mNear))));
	#ifdef TRANSFORM_UNIFORM_SCALING
	ray.Origin = gCameraToWorld.mTranslation;
	#else
	ray.Origin = float3(gCameraToWorld.m[0][3], gCameraToWorld.m[1][3], gCameraToWorld.m[2][3]);
	#endif
	ray.TMin = gProjection.mNear;
	ray.TMax = 1.#INF;
	return ray;
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

#define ray_query_t RayQuery<RAY_FLAG_FORCE_OPAQUE>
bool do_ray_query(inout ray_query_t rayQuery, differential3 dP, differential3 dD) {
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
				InstanceData instance = gInstances[rayQuery.CandidateInstanceIndex()];
				MaterialData md = gMaterials[instance.mMaterialIndex];
				ImageIndices inds;
				inds.v = md.mImageIndices;
				uint i = inds.albedo();
				if (i < gImageCount) {
					SurfaceSample sfc = surface_attributes(instance, gVertices, gIndices, rayQuery.CandidateInstanceIndex(), rayQuery.CandidateTriangleBarycentrics(), rayQuery.WorldRayOrigin(), dP, dD);
					if (gImages[NonUniformResourceIndex(i)].SampleLevel(gSampler, float2(sfc.v.mU, sfc.v.mV), 0).a >= md.mAlphaCutoff)
						rayQuery.CommitNonOpaqueTriangleHit();
				}
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
	RayDesc ray = create_ray(uv);
	differential3 dP;
	dP.dx = 0;
	dP.dy = 0;
	differential3 dD;
	dD.dx = create_ray(uv + float2(1/(float)resolution.x, 0)).Direction - ray.Direction;
	dD.dy = create_ray(uv + float2(0, 1/(float)resolution.y)).Direction - ray.Direction;

	uint4 rng = uint4(index.xy, gPushConstants.gRandomSeed, index.x + index.y);
	gRNGSeed[index.xy] = rng;

	uint instanceIndex = -1;
	uint primitiveIndex = 0;
	float2 bary = 0;
	float3 normal = 0;
	float4 z = float4(1.#INF, 0, 0, 0);
	float2 prevUV = uv;
	ray_query_t rayQuery;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
	if (do_ray_query(rayQuery, dP, dD)) {
		instanceIndex = rayQuery.CommittedInstanceID();
 		InstanceData instance = gInstances[instanceIndex];
		float3 pos = 0;
		z.x = rayQuery.CommittedRayT();
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// light
			primitiveIndex = -1;
			pos = ray.Origin + ray.Direction*rayQuery.CommittedRayT();
		} else {
			primitiveIndex = rayQuery.CommittedPrimitiveIndex();
			bary = rayQuery.CommittedTriangleBarycentrics();
			SurfaceSample sfc = surface_attributes(instance, gVertices, gIndices, primitiveIndex, bary, ray.Origin, dP, dD);
			
			//MaterialData md = gMaterials[instance.mMaterialIndex];
			//ImageIndices inds;
			//inds.v = md.mImageIndices;
			//uint i = inds.albedo();
			//i = inds.normal();
			//if (i < gImageCount) {
			//	float3 bump = sample_image(gImages[NonUniformResourceIndex(i)], sfc).xyz*2 - 1;
			//	bump.xy *= md.mNormalScale;
			//	sfc.v.mNormal = normalize(bump.x*sfc.v.mTangent.xyz + bump.y*cross(sfc.v.mTangent.xyz, sfc.v.mNormal)*sfc.v.mTangent.w + sfc.v.mNormal);
			//}

			pos = sfc.v.mPosition;
			normal = sfc.v.mNormal;
			z.z = transform_vector(gWorldToCamera, dP.dx).z;
			z.w = transform_vector(gWorldToCamera, dP.dy).z;
			//z.zw = 1/abs(normalize(transform_vector(gWorldToCamera, sfc.v.mNormal)).z);
		}

		float3 prevCamPos = transform_point(tmul(gWorldToPrevCamera, instance.mMotionTransform), pos);
		z.y = length(prevCamPos);
		
		float4 prevScreenPos = project_point(gPrevProjection, prevCamPos);
		prevScreenPos.y = -prevScreenPos.y;
		prevUV = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
	} else
		bary = float2(atan2(ray.Direction.z, ray.Direction.x)/M_PI *.5 + .5, acos(clamp(ray.Direction.y, -1, 1)) / M_PI);

	gVisibility[index.xy] = uint4(instanceIndex, primitiveIndex, asuint(bary));
	gNormal[index.xy] = float4(normal,1);
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
	float3 reflDir = 0;
	if (vis.x == -1) {
		if (gEnvironmentMap < gImageCount)
			radiance = pow(gImages[gEnvironmentMap].SampleLevel(gSampler, asfloat(vis.zw), 0).rgb, 1/gPushConstants.gEnvironmentMapGamma)*gPushConstants.gEnvironmentMapExposure;
	} else if (vis.y == -1) {
		radiance = gLights[gInstances[vis.x].mIndexStride>>8].mEmission;
	} else {
		uint4 rng = gRNGSeed[index.xy];
		
		float3 throughput = 1;
		float pdf_bsdf = 1;
		float3 absorption = 0;
		float hit_t = 0;
		const bool sample_bg = (gSamplingFlags & SAMPLE_FLAG_BG_IS) && gEnvironmentMap < gImageCount;
		const bool sample_lights = (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;
		const uint reservoirSamples = gSamplingFlags >> SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET;

		ray_query_t rayQuery;
		
		RayDesc ray = create_ray((index.xy + 0.5)/float2(resolution));
		differential3 dP;
		dP.dx = 0;
		dP.dy = 0;
		differential3 dD;
		dD.dx = create_ray((index.xy + 0.5 + uint2(1,0))/float2(resolution)).Direction - ray.Direction;
		dD.dy = create_ray((index.xy + 0.5 + uint2(0,1))/float2(resolution)).Direction - ray.Direction;
		for (uint bounce_index = 0; bounce_index < gMaxBounces; bounce_index++) {
			InstanceData instance = gInstances[vis.x];
			SurfaceSample sfc = surface_attributes(instance, gVertices, gIndices, vis.y, asfloat(vis.zw), ray.Origin, dP, dD);
			MaterialData material = sample_image_attributes(instance, sfc);

			if (bounce_index == 0) {
				ray.Direction = sfc.v.mPosition - ray.Origin;
				hit_t = length(ray.Direction);
				ray.Direction /= hit_t;
				albedo = material.mAlbedo;
				
				if (any(material.mEmission > 0) && sfc.front_face) {
					LightData light = gLights[instance.mIndexStride>>8];
					PackedLightData p;
					p.v = light.mPackedData;
					radiance += throughput * material.mEmission;
				}
			}
			
			throughput *= exp(-absorption * hit_t);
			
			if (all(material.mAlbedo <= 0)) break;

			DisneyBSDF bsdf = to_disney_bsdf(material);
			if (!sfc.front_face) bsdf.eta = 1/bsdf.eta;
			float3 omega_out = -ray.Direction;

			bool apply_mis_weight = false;
			
			// sample a light
			if (!bsdf.is_delta() && (sample_bg || sample_lights)) {
				LightSample ls;
				float pdf_pick;
				float p_hat;
				if (reservoirSamples > 0 && bounce_index == 0) {
					Reservoir r;
					r.w_sum = 0;
					r.M = 0;
					//float2 res = gReservoirs[index.xy];
					//r.w_sum = res.x;
					//r.M = asuint(res.y);
					for (uint i = 0; i < reservoirSamples; i++) {
						uint4 rng_i = rng;

						float pdf_pick_i;
						LightSample ls_i = get_light_sample(rng, sfc.v.mPosition, dP, dD, pdf_pick_i);
						if (ls_i.pdf <= 0) continue;

						float pdf_bsdf_i;
						ls_i.radiance *= bsdf.Evaluate(omega_out, sfc.v.mNormal, ls_i.omega_in, pdf_bsdf_i) * abs(dot(ls_i.omega_in, sfc.v.mNormal)) / ls_i.pdf;
						float p_hat_i = luminance(ls_i.radiance);
						if (r.update(next_rng_sample(rng), vis, rng_i, p_hat_i	/ pdf_pick_i)) {
							ls = ls_i;
							p_hat = p_hat_i;
							pdf_pick = pdf_pick_i;
							pdf_bsdf = pdf_bsdf_i;
						}
					}
					ls.radiance *= r.w_sum / p_hat / r.M;
					apply_mis_weight = true;
					
				} else {
					ls = get_light_sample(rng, sfc.v.mPosition, dP, dD, pdf_pick);
					ls.radiance *= bsdf.Evaluate(omega_out, sfc.v.mNormal, ls.omega_in, pdf_bsdf) * abs(dot(ls.omega_in, sfc.v.mNormal)) / (ls.pdf*pdf_pick);
					if (!ls.punctual && bounce_index < gMaxBounces-1)
						ls.radiance *= powerHeuristic(ls.pdf*pdf_pick, pdf_bsdf);
					apply_mis_weight = true;
				}

				if (pdf_bsdf > 0 && ls.pdf > 0) {
					RayDesc shadowRay;
					shadowRay.Origin = ray_offset(sfc.v.mPosition, sfc.Ng*sign(dot(sfc.Ng, ls.omega_in)));
					shadowRay.Direction = ls.omega_in;
					shadowRay.TMin = 0;
					shadowRay.TMax = ls.dist - 1e-4;
					rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
					if (!do_ray_query(rayQuery, dP, dD))
						radiance += throughput * ls.radiance;
				}
			}

			// sample the bsdf
			float3 H, omega_in;
			uint flag;
			float3 f = bsdf.Sample(rng, omega_out, sfc.v.mNormal, sfc.v.mTangent, omega_in, pdf_bsdf, flag, H);
			if (pdf_bsdf == 0) break;

			throughput *= f * abs(dot(sfc.v.mNormal, omega_in)) / pdf_bsdf;
			
			if (all(throughput < 1e-6)) break;

			if (sign(dot(omega_in, H)) < 0) {
				absorption = material.mAbsorption;
				ray.Origin = ray_offset(sfc.v.mPosition, -sfc.Ng);
				dD.dx = refract(dD.dx, H, bsdf.eta);
				dD.dy = refract(dD.dy, H, bsdf.eta);
			} else {
				absorption = 0;
				ray.Origin = ray_offset(sfc.v.mPosition, sfc.Ng);
				dD.dx = reflect(dD.dx, H);
				dD.dy = reflect(dD.dy, H);
			}
			ray.Direction = omega_in;
			ray.TMin = 0;
			ray.TMax = 1.#INF;
			rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
			if (!do_ray_query(rayQuery, dP, dD)) {
				if (gEnvironmentMap < gImageCount) {
					float theta = acos(clamp(ray.Direction.y, -1, 1));
					float2 uv = float2(atan2(ray.Direction.z, ray.Direction.x)/M_PI *.5 + .5, theta / M_PI);
					float3 bg = pow(gImages[gEnvironmentMap].SampleLevel(gSampler, uv, 0).rgb, 1/gPushConstants.gEnvironmentMapGamma)*gPushConstants.gEnvironmentMapExposure;
					if (sample_bg && apply_mis_weight) {
						float pdf_light = environment_pdf(ray.Direction, uv);
						if (sample_lights) pdf_light *= 0.5;
						bg *= powerHeuristic(pdf_bsdf, pdf_light);
					}
					radiance += throughput * bg;
				}
				break;
			}

			hit_t = rayQuery.CommittedRayT();
			
			if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
				LightData light = gLights[gInstances[rayQuery.CommittedInstanceIndex()].mIndexStride>>8];
				if (sample_lights && apply_mis_weight) {
					PackedLightData p;
					p.v = light.mPackedData;
					float pdf_light = hit_t*hit_t / (2*M_PI*p.radius()*p.radius());
					pdf_light *= 1 / (float)gPushConstants.gLightCount;
					if (sample_bg) pdf_light *= 0.5;
					throughput *= powerHeuristic(pdf_bsdf, sample_bg);
				}
				radiance += throughput * light.mEmission;
				break;
			}
			if (any(material.mEmission > 0) && sfc.front_face) {
				LightData light = gLights[instance.mIndexStride>>8];
				PackedLightData p;
				p.v = light.mPackedData;
				if (sample_lights && apply_mis_weight) {
					float pdf_light = hit_t*hit_t / (dot(-ray.Direction, sfc.v.mNormal) * sfc.area);
					pdf_light *= (1 / (float)p.prim_count()) * (1 / (float)gPushConstants.gLightCount);
					if (sample_bg) pdf_light *= 0.5;
					radiance += throughput * material.mEmission * powerHeuristic(pdf_bsdf, pdf_light);
				} else
					radiance += throughput * material.mEmission;
			}

			if (gSamplingFlags & SAMPLE_FLAG_RR) {
				float l = luminance(throughput);
				if (next_rng_sample(rng) > l)
					break;
				throughput /= l;
			}

			vis.x = rayQuery.CommittedInstanceIndex();
			vis.y = rayQuery.CommittedPrimitiveIndex();
			vis.zw = asuint(rayQuery.CommittedTriangleBarycentrics());
		}
	}

	if (gDemodulateAlbedo) {
		if (albedo.r > 0) radiance.r /= albedo.r;
		if (albedo.g > 0) radiance.g /= albedo.g;
		if (albedo.b > 0) radiance.b /= albedo.b;
	}

	gRadiance[index.xy] = float4(radiance, 0);
	gAlbedo[index.xy] = float4(albedo, 1);
}