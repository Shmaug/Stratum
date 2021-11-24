#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E visibility
#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E lighting

#include "rtscene.hlsli"

#define gImageCount 1024

[[vk::constant_id(0)]] const uint gMaxBounces = 3;
[[vk::constant_id(1)]] const uint gSamplingFlags = 0xFF;
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
[[vk::binding(11)]] RWTexture2D<float4> gReservoirs;
[[vk::binding(12)]] RWTexture2D<uint4> gReservoirRNG;

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

float4 sample_image(Texture2D<float4> img, SurfaceSample sfc) {
	//return img.SampleLevel(gSampler, float2(sfc.v.mU, sfc.v.mV), 0);
	return img.SampleGrad(gSampler, float2(sfc.v.mU, sfc.v.mV), sfc.dUV.dx, sfc.dUV.dy);
}

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

struct LightSample {
	float3 radiance;
	float pdf;
	float3 omega_in;
	float dist;
	float attenuation;
	bool punctual;
};
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
	ls.attenuation = 1;
	ls.punctual = false;
	return ls;
}
LightSample sample_light(inout uint4 rng, float3 P, differential3 dP, differential3 dD, out float pdf_light) {
	pdf_light = 1/(float)gPushConstants.gLightCount;
	LightData light = gLights[min(uint(next_rng_sample(rng) * gPushConstants.gLightCount), gPushConstants.gLightCount-1)];
	PackedLightData p;
	p.v = light.mPackedData;
	
	LightSample ls;
	ls.radiance = light.mEmission;
	
	switch (light.mType) {
		case LIGHT_TYPE_DISTANT: {
			ls.punctual = true;
			ls.omega_in = transform_vector(light.mLightToWorld, float3(0,0,sign(light.mShadowProjection.mNear)));
			ls.dist = 1.#INF;
			ls.attenuation = 1;
			if (p.radius() > 0) {
				float3 T, B;
				Onb(ls.omega_in, T, B);
				float cosThetaMax = p.radius()/(2*M_PI);
				float3 s = UniformSampleCone(next_rng_sample(rng), next_rng_sample(rng), cosThetaMax);
				ls.omega_in = T*s.x + B*s.y + ls.omega_in*s.z;
				ls.pdf = 1 / p.radius();
			} else
				ls.pdf = 1;
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

				float sinThetaMax2 = p.radius()*p.radius() / distSq;
				float cosThetaMax = sqrt(max(0., 1. - sinThetaMax2));
				float3 s = UniformSampleCone(next_rng_sample(rng), next_rng_sample(rng), cosThetaMax);
				ls.omega_in = T*s.x + B*s.y + ls.omega_in*s.z;
				ls.pdf = 1 / (2*M_PI * (1 - cosThetaMax));
				ls.dist -= p.radius();
				ls.attenuation = 1/(ls.dist*ls.dist);
				ls.punctual = false;
			} else {
				ls.attenuation = 1/distSq;
				ls.pdf = 1;
				ls.punctual = true;
			}
			break;
		}

		case LIGHT_TYPE_MESH: {
			ls.punctual = false;
			uint primIndex = min(next_rng_sample(rng)*p.prim_count(), p.prim_count() - 1);
			pdf_light /= (float)p.prim_count();
			
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
				ls.attenuation = lnv*lsfc.area / distSq;
				ls.pdf = distSq / (lnv*lsfc.area);
			} else
				ls.pdf = 0;
			break;
		}
	}

	return ls;
}
LightSample get_light_sample(inout uint4 rng, float3 P, differential3 dP, differential3 dD, out float pdf_light) {
	bool sample_bg = (gSamplingFlags & SAMPLE_FLAG_BG_IS) && gEnvironmentMap < gImageCount;
	bool sample_lights = (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

	LightSample ls;
	pdf_light = 1;
	
	if (sample_bg && !sample_lights)
		ls = sample_environment(rng);
	else if (sample_lights && !sample_bg)
		ls = sample_light(rng, P, dP, dD, pdf_light);
	else {
		if (next_rng_sample(rng) < 0.5)
			ls = sample_environment(rng);
		else
			ls = sample_light(rng, P, dP, dD, pdf_light);
		pdf_light /= 2;
	}
	return ls;
}

class Reservoir {
	uint4 y;
	float p_hat_y;
	float w_sum;
	float M;
	float W;

	void load(uint2 index) {
		float4 r = gReservoirs[index];
		w_sum = r.x;
		M = r.y;
		p_hat_y = r.z;
		W = r.w;
		y = gReservoirRNG[index];
	}
	void store(uint2 index) {
		gReservoirs[index] = float4(w_sum, M, p_hat_y, W);
		gReservoirRNG[index] = y;
	}
	bool update(float r1, uint4 xi, float p_hat_xi, float p_xi) {
		float wi = p_hat_xi / p_xi;
		w_sum += wi;
		M++;
		if (r1 < wi/w_sum) {
			y = xi;
			p_hat_y = p_hat_xi;
			return true;
		}
		return false;
	}
	void ris(uint reservoirSamples, inout ray_query_t rayQuery, inout uint4 rng, DisneyBSDF bsdf, float3 omega_out, SurfaceSample sfc, differential3 dP, differential3 dD) {
		RayDesc shadowRay;
		for (uint i = 0; i < reservoirSamples; i++) {
			uint4 xi = rng;

			float pdf_light_i;
			LightSample ls_i = get_light_sample(rng, sfc.v.mPosition, dP, dD, pdf_light_i);
			if (ls_i.pdf <= 0) continue;

			float pdf_bsdf_i;
			float p_hat_xi = luminance(bsdf.Evaluate(omega_out, sfc.v.mNormal, ls_i.omega_in, pdf_bsdf_i) * ls_i.radiance) * abs(dot(ls_i.omega_in, sfc.v.mNormal)) * ls_i.attenuation;
			if (update(next_rng_sample(rng), xi, p_hat_xi, ls_i.pdf)) {
				shadowRay.Direction = ls_i.omega_in;
				shadowRay.TMax = ls_i.dist - 1e-4;
			}
		}
		shadowRay.Origin = ray_offset(sfc.v.mPosition, sfc.Ng*sign(dot(sfc.Ng, shadowRay.Direction)));
		shadowRay.TMin = 0;
		rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
		if (do_ray_query(rayQuery, dP, dD)) {
			w_sum = 0;
			M = 0;
			W = 0;
		} else
			W = w_sum / M / p_hat_y;
	}
};

class PathState {
	InstanceData instance;
	SurfaceSample sfc;
	MaterialData material;
	DisneyBSDF bsdf;
	
	RayDesc ray;
	differential3 dP, dD;
	
	uint4 rng;
	uint2 coord;
	float3 throughput;
	uint bounce_index;
	float3 radiance;
	float3 absorption;
	float hit_t;
		
	void create_ray(uint2 index, uint2 resolution) {
		coord = index;
		radiance = 0;
		throughput = 1;
		bounce_index = 0;

		float2 sz = 2 / float2(resolution);
		float3 screenPos = float3((index + 0.5)*sz - 1, 1);
		float3 screenPos_dx = float3((index + uint2(1,0) + 0.5)*sz - 1, 1);
		float3 screenPos_dy = float3((index + uint2(0,1) + 0.5)*sz - 1, 1);
		screenPos.y = -screenPos.y;
		screenPos_dx.y = -screenPos_dx.y;
		screenPos_dy.y = -screenPos_dy.y;

		ray.Direction = normalize(transform_vector(gCameraToWorld, float3(back_project(gProjection, screenPos).xy, sign(gProjection.mNear))));
		#ifdef TRANSFORM_UNIFORM_SCALING
		ray.Origin = gCameraToWorld.mTranslation;
		#else
		ray.Origin = float3(gCameraToWorld.m[0][3], gCameraToWorld.m[1][3], gCameraToWorld.m[2][3]);
		#endif
		ray.TMin = gProjection.mNear;
		ray.TMax = 1.#INF;
		
		dP.dx = 0;
		dP.dy = 0;
		dD.dx = normalize(transform_vector(gCameraToWorld, float3(back_project(gProjection, screenPos_dx).xy, sign(gProjection.mNear)))) - ray.Direction;
		dD.dy = normalize(transform_vector(gCameraToWorld, float3(back_project(gProjection, screenPos_dy).xy, sign(gProjection.mNear)))) - ray.Direction;
	}

	void load_sfc_bsdf(uint primitiveIndex, float2 bary) {
		sfc = surface_attributes(instance, gVertices, gIndices, primitiveIndex, bary, ray.Origin, dP, dD);

		material = gMaterials[instance.mMaterialIndex];
		ImageIndices inds;
		inds.v = material.mImageIndices;
		uint i = inds.albedo();
		if (i < gImageCount) {
			float4 a = sample_image(gImages[NonUniformResourceIndex(i)], sfc);
			material.mAlbedo *= a.rgb;
			material.mTransmission = 1 - a.w*(1 - material.mTransmission); 
		}
		i = inds.metallic();
		if (i < gImageCount) material.mMetallic = saturate(material.mMetallic*sample_image(gImages[NonUniformResourceIndex(i)], sfc)[inds.metallic_channel()]);
		i = inds.roughness();
		if (i < gImageCount) material.mRoughness = saturate(material.mRoughness*sample_image(gImages[NonUniformResourceIndex(i)], sfc)[inds.roughness_channel()]);
		i = inds.emission();
		if (i < gImageCount) material.mEmission *= sample_image(gImages[NonUniformResourceIndex(i)], sfc).rgb;
		i = inds.normal();
		if (i < gImageCount) {
			float3 bump = sample_image(gImages[NonUniformResourceIndex(i)], sfc).xyz*2 - 1;
			bump.xy *= material.mNormalScale;
			sfc.v.mNormal = normalize(bump.x*sfc.v.mTangent.xyz + bump.y*cross(sfc.v.mTangent.xyz, sfc.v.mNormal)*sfc.v.mTangent.w + sfc.v.mNormal);
		}
		// ortho-normalize
		sfc.v.mTangent.xyz = normalize(sfc.v.mTangent.xyz - sfc.v.mNormal * dot(sfc.v.mNormal, sfc.v.mTangent.xyz));

		bsdf = to_disney_bsdf(material);
		if (!sfc.front_face) bsdf.eta = 1/bsdf.eta;
	}

	void sample_reservoir(Reservoir r) {
		if (r.W > 0) {
			float pdf_light;
			LightSample ls = get_light_sample(r.y, sfc.v.mPosition, dP, dD, pdf_light);
			float pdf_bsdf;
			float3 f = bsdf.Evaluate(-ray.Direction, sfc.v.mNormal, ls.omega_in, pdf_bsdf);
			radiance += throughput * ls.radiance * f * abs(dot(ls.omega_in, sfc.v.mNormal)) * ls.attenuation * r.W;
		}
	}

	void sample_light_mis(inout ray_query_t rayQuery) {
		float pdf_light;
		LightSample ls = get_light_sample(rng, sfc.v.mPosition, dP, dD, pdf_light);
		pdf_light *= ls.pdf;
		RayDesc shadowRay;
		shadowRay.Origin = ray_offset(sfc.v.mPosition, sfc.Ng*sign(dot(sfc.Ng, ls.omega_in)));
		shadowRay.Direction = ls.omega_in;
		shadowRay.TMin = 0;
		shadowRay.TMax = ls.dist - 1e-4;
		rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, shadowRay);
		if (!do_ray_query(rayQuery, dP, dD)) {
			float pdf_bsdf;
			float3 f = bsdf.Evaluate(-ray.Direction, sfc.v.mNormal, ls.omega_in, pdf_bsdf);
			if (!ls.punctual)
				f *= misHeuristic(pdf_light, pdf_bsdf);
			radiance += throughput * ls.radiance * f * abs(dot(ls.omega_in, sfc.v.mNormal)) / pdf_light;
		}
	}

	void sample_bsdf(out float3 omega_in, out float pdf_bsdf) {
		float3 H;
		uint flag;
		float3 f = bsdf.Sample(rng, -ray.Direction, sfc.v.mNormal, sfc.v.mTangent, omega_in, pdf_bsdf, flag, H);
		if (pdf_bsdf == 0) { throughput = 0; return; }
		throughput *= f * abs(dot(sfc.v.mNormal, omega_in)) / pdf_bsdf;

		if (all(throughput < 1e-6)) return;

		if (sign(dot(omega_in, sfc.Ng)) < 0) {
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
	}

	void next_bounce(inout ray_query_t rayQuery, float3 omega_in, float pdf_bsdf, bool apply_mis_weight) {
		const bool sample_bg = (gSamplingFlags & SAMPLE_FLAG_BG_IS) && gEnvironmentMap < gImageCount;
		const bool sample_lights = (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;

		bounce_index++;

		ray.Origin = ray_offset(sfc.v.mPosition, sfc.Ng*sign(dot(sfc.Ng, omega_in)));
		ray.Direction = omega_in;
		ray.TMin = 0;
		ray.TMax = 1.#INF;
		rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);

		// environment light
		if (!do_ray_query(rayQuery, dP, dD)) {
			if (gEnvironmentMap < gImageCount) {
				float theta = acos(clamp(omega_in.y, -1, 1));
				float2 uv = float2(atan2(omega_in.z, omega_in.x)/M_PI *.5 + .5, theta / M_PI);
				float3 bg = pow(gImages[gEnvironmentMap].SampleLevel(gSampler, uv, 0).rgb, 1/gPushConstants.gEnvironmentMapGamma)*gPushConstants.gEnvironmentMapExposure;
				float w = 1;
				if (sample_bg && apply_mis_weight) {
					float pdf_light = environment_pdf(omega_in, uv);
					if (sample_lights) pdf_light *= 0.5;
					w = misHeuristic(pdf_bsdf, pdf_light);
				}
				radiance += throughput * bg * w;
			}
			throughput = 0;
		}

		instance = gInstances[rayQuery.CommittedInstanceIndex()];
		hit_t = rayQuery.CommittedRayT();
		
		// sphere light
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			LightData light = gLights[instance.mIndexStride>>8];
			float w = 1;
			if (sample_lights && apply_mis_weight) {
				PackedLightData p;
				p.v = light.mPackedData;
				float sinThetaMax2 = p.radius()*p.radius() / (hit_t*hit_t);
				float cosThetaMax = sqrt(max(0., 1. - sinThetaMax2));
				float pdf_light = 1 / (2*M_PI * (1 - cosThetaMax));
				pdf_light /= (float)gPushConstants.gLightCount;
				if (sample_bg) pdf_light *= 0.5;
				w = misHeuristic(pdf_bsdf, pdf_light);
			}
			radiance += throughput * light.mEmission * w;
			throughput = 0;
		}

		load_sfc_bsdf(rayQuery.CommittedPrimitiveIndex(), rayQuery.CommittedTriangleBarycentrics());

		// mesh light
		if (any(material.mEmission > 0) && sfc.front_face) {
			LightData light = gLights[instance.mIndexStride>>8];
			PackedLightData p;
			p.v = light.mPackedData;
			float w = 1;
			if (sample_lights && apply_mis_weight) {
				float pdf_light = (hit_t*hit_t) / (sfc.area * dot(sfc.v.mNormal, -omega_in));
				pdf_light /= (float)gPushConstants.gLightCount * (float)p.prim_count();
				if (sample_bg) pdf_light *= 0.5;
				w = misHeuristic(pdf_bsdf, pdf_light);
			}
			radiance += throughput * material.mEmission * w;
		}
	}
};

[numthreads(8,8,1)]
void visibility(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gVisibility.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	PathState state;
	state.create_ray(index.xy, resolution);
	state.rng = uint4(index.xy, gPushConstants.gRandomSeed, index.x + index.y);
	gRNGSeed[index.xy] = state.rng;

	uint4 vis = uint4(-1, 0, 0, 0);
	float3 normal = 0;
	float4 z = float4(1.#INF, 0, 0, 0);
	float2 prevUV = -1;
	ray_query_t rayQuery;
	rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, state.ray);
	if (do_ray_query(rayQuery, state.dP, state.dD)) {
		vis.x = rayQuery.CommittedInstanceID();
		z.x = rayQuery.CommittedRayT();
 		state.instance = gInstances[vis.x];
		if (rayQuery.CommittedStatus() == COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
			// light
			vis.y = -1;
		} else {
			vis.y = rayQuery.CommittedPrimitiveIndex();
			vis.zw = asuint(rayQuery.CommittedTriangleBarycentrics());
			state.load_sfc_bsdf(vis.y, asfloat(vis.zw));

			normal = state.sfc.v.mNormal;

			float3 prevCamPos = transform_point(tmul(gWorldToPrevCamera, state.instance.mMotionTransform), state.sfc.v.mPosition);
			float4 prevScreenPos = project_point(gPrevProjection, prevCamPos);
			prevScreenPos.y = -prevScreenPos.y;
			prevUV = (prevScreenPos.xy / prevScreenPos.w)*.5 + .5;
			
			z.y = length(prevCamPos);
			z.z = transform_vector(gWorldToCamera, state.dP.dx).z;
			z.w = transform_vector(gWorldToCamera, state.dP.dy).z;
			//z.zw = 1/abs(normalize(transform_vector(gWorldToCamera, sfc.v.mNormal)).z);
			
			const uint reservoirSamples = gSamplingFlags >> SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET;
			if (reservoirSamples > 0) {
				Reservoir r;
				r.M = 0;
				r.w_sum = 0;
				r.ris(reservoirSamples, rayQuery, state.rng, state.bsdf, -state.ray.Direction, state.sfc, state.dP, state.dD);
				r.store(index.xy);
			}
		}
	}

	gVisibility[index.xy] = vis;
	gNormal[index.xy] = float4(normal,1);
	gZ[index.xy] = z;
	gPrevUV[index.xy] = prevUV;
}

[numthreads(8,8,1)]
void lighting(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gRadiance.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	PathState state;
	state.create_ray(index.xy, resolution);
	
	uint4 vis = gVisibility[index.xy];
	
	float3 albedo = 1;
	float3 reflDir = 0;
	if (vis.x == -1) {
		if (gEnvironmentMap < gImageCount)
			state.radiance = pow(gImages[gEnvironmentMap].SampleLevel(gSampler, asfloat(vis.zw), 0).rgb, 1/gPushConstants.gEnvironmentMapGamma)*gPushConstants.gEnvironmentMapExposure;
	} else if (vis.y == -1) {
		state.radiance = gLights[gInstances[vis.x].mIndexStride>>8].mEmission;
	} else {
		state.rng = gRNGSeed[index.xy];
		state.instance = gInstances[vis.x];
		state.load_sfc_bsdf(vis.y, asfloat(vis.zw));

		state.hit_t = length(state.sfc.v.mPosition - state.ray.Origin);
		
		if (!state.sfc.front_face) {
			state.absorption = state.material.mAbsorption;
			state.throughput *= exp(-state.absorption * state.hit_t);
		} else {
			state.absorption = 0;
			if (any(state.material.mEmission > 0)) {
				LightData light = gLights[state.instance.mIndexStride>>8];
				PackedLightData p;
				p.v = light.mPackedData;
				state.radiance += state.material.mEmission;
			}
		}
	
		albedo = state.material.mAlbedo;

		const bool sample_bg = (gSamplingFlags & SAMPLE_FLAG_BG_IS) && gEnvironmentMap < gImageCount;
		const bool sample_lights = (gSamplingFlags & SAMPLE_FLAG_LIGHT_IS) && gPushConstants.gLightCount > 0;
		const uint reservoirSamples = gSamplingFlags >> SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET;
		
		// trace path
		ray_query_t rayQuery;
		do {
			bool apply_mis_weight = false;
			if (!state.bsdf.is_delta()) {
				if (reservoirSamples > 0 && state.bounce_index == 0) {
					Reservoir r;
					r.load(index.xy);
					state.sample_reservoir(r);
				}
				if (sample_bg || sample_lights) {
					state.sample_light_mis(rayQuery);
					apply_mis_weight = true;
				}
			}
			
			if (state.bounce_index >= gMaxBounces) break;

			float3 omega_in;
			float pdf_bsdf;
			if (gSamplingFlags & SAMPLE_FLAG_BSDF_IS)
				state.sample_bsdf(omega_in, pdf_bsdf);
			else
				break; // TODO: hemisphere sampling or something basic

			state.next_bounce(rayQuery, omega_in, pdf_bsdf, apply_mis_weight);
		
			if (gSamplingFlags & SAMPLE_FLAG_RR) {
				float l = luminance(state.throughput);
				if (next_rng_sample(state.rng) > l)
					break;
				state.throughput /= l;
			}
		} while (all(state.throughput > 1e-6));
	}

	if (gDemodulateAlbedo) {
		if (albedo.r > 0) state.radiance.r /= albedo.r;
		if (albedo.g > 0) state.radiance.g /= albedo.g;
		if (albedo.b > 0) state.radiance.b /= albedo.b;
	}

	gRadiance[index.xy] = float4(state.radiance, 0);
	gAlbedo[index.xy] = float4(albedo, 1);
}