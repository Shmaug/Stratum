#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -fspv-extension=SPV_EXT_descriptor_indexing -fspv-extension=SPV_KHR_ray_tracing -fspv-extension=SPV_KHR_ray_query -T cs_6_7 -E main

#include "rtscene.hlsli"

#define DEBUG_NONE 							0
#define DEBUG_ALBEDO 						1
#define DEBUG_METALLIC 					2
#define DEBUG_ROUGHNESS 				3
#define DEBUG_NORMALS_GEOMETRY 	4
#define DEBUG_NORMALS_SMOOTH 		5
#define DEBUG_NORMALS_BUMP 			6
#define DEBUG_TANGENTS          7
#define DEBUG_TANGENTS_BUMP   	8
#define DEBUG_EMISSION 					9
#define DEBUG_PDF 							10
#define DEBUG_BG_PDF 						11

#define gImageCount 64
[[vk::constant_id(0)]] const uint gSampleCount = 1;
[[vk::constant_id(1)]] const uint gMaxBounces = 5;
[[vk::constant_id(2)]] const uint gRussianRouletteDepth = 3;
[[vk::constant_id(3)]] const uint gDebugMode = 0;
[[vk::constant_id(4)]] const uint gSamplingFlags = SAMPLE_FLAG_BG_IS;
[[vk::constant_id(6)]] const uint gEnvironmentMap = -1;
[[vk::constant_id(7)]] const bool gDemodulateAlbedo = false;

RaytracingAccelerationStructure gScene;
StructuredBuffer<VertexData> gVertices;
ByteAddressBuffer gIndices;
StructuredBuffer<InstanceData> gInstances;
StructuredBuffer<MaterialData> gMaterials;
StructuredBuffer<LightData> gLights;
RWTexture2D<float4> gSamples;
RWTexture2D<float4> gNormalId;
RWTexture2D<float2> gZ;
RWTexture2D<float4> gAlbedo;
RWTexture2D<float2> gPrevUV;
SamplerState gSampler;
Texture2D<float2> gEnvironmentConditionalDistribution;
Texture2D<float2> gEnvironmentMarginalDistribution;
Texture2D<float4> gImages[gImageCount];

cbuffer gCameraData {
	TransformData gCameraToWorld;
	ProjectionData gProjection;
	TransformData gPrevCameraToWorld;
	ProjectionData gPrevProjection;
};

[[vk::push_constant]] const struct {
	uint gLightCount;
	uint gRandomSeed;
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

struct SurfaceData {
	InstanceData instance;
	VertexData v;
	float3 Ng;
	float area;
};

float3 ray_offset(float3 P, float3 Ng) {
#ifdef __INTERSECTION_REFINE__
  const float epsilon_f = 1e-5f;
  /* ideally this should match epsilon_f, but instancing and motion blur
   * precision makes it problematic */
  const float epsilon_test = 1.0f;
  const int epsilon_i = 32;

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
#else
  const float epsilon_f = 1e-4f;
  return P + epsilon_f * Ng;
#endif
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

SurfaceData surface_attributes(uint instanceIndex, uint primitiveIndex, float2 bary) {
	SurfaceData sfc;
	sfc.instance = gInstances[NonUniformResourceIndex(instanceIndex)];
	// load indices
	uint offsetBytes = sfc.instance.mIndexByteOffset + primitiveIndex*3*sfc.instance.mIndexStride;
	uint3 tri;
	if (sfc.instance.mIndexStride == 2) {
		// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl
		uint dwordAlignedOffset = offsetBytes & ~3;    
		uint2 four16BitIndices = gIndices.Load2(dwordAlignedOffset);
		if (dwordAlignedOffset == offsetBytes) {
				tri.x = four16BitIndices.x & 0xffff;
				tri.y = (four16BitIndices.x >> 16) & 0xffff;
				tri.z = four16BitIndices.y & 0xffff;
		} else {
				tri.x = (four16BitIndices.x >> 16) & 0xffff;
				tri.y = four16BitIndices.y & 0xffff;
				tri.z = (four16BitIndices.y >> 16) & 0xffff;
		}
	} else
		tri = gIndices.Load3(offsetBytes);
	tri += sfc.instance.mFirstVertex;

	// load vertex data
	sfc.v         = gVertices[tri.x];
	VertexData v1 = gVertices[tri.y];
	VertexData v2 = gVertices[tri.z];

	v1.mPositionU -= sfc.v.mPositionU;
	v2.mPositionU -= sfc.v.mPositionU;
	v1.mNormalV -= sfc.v.mNormalV;
	v2.mNormalV -= sfc.v.mNormalV;
	v1.mTangent -= sfc.v.mTangent;
	v2.mTangent -= sfc.v.mTangent;
	
	sfc.v.mPositionU += v1.mPositionU*bary.x + v2.mPositionU*bary.y;
	sfc.v.mNormalV   += v1.mNormalV*bary.x + v2.mNormalV*bary.y;
	sfc.v.mTangent   += v1.mTangent*bary.x + v2.mTangent*bary.y;
	sfc.Ng = cross(v1.mPositionU.xyz, v2.mPositionU.xyz);

	sfc.v.mPositionU.xyz = transform_point(sfc.instance.mTransform, sfc.v.mPositionU.xyz);
	sfc.v.mNormalV.xyz = normalize(transform_vector(sfc.instance.mTransform, sfc.v.mNormalV.xyz));
	sfc.v.mTangent.xyz = transform_vector(sfc.instance.mTransform, sfc.v.mTangent.xyz);
	sfc.Ng = transform_vector(sfc.instance.mTransform, sfc.Ng);

	sfc.area = length(sfc.Ng);
	sfc.Ng /= sfc.area;
	sfc.area /= 2;
	return sfc;
}
MaterialData material_attributes(inout SurfaceData sfc) {
	// sample textures
	float3 texcoord = float3(sfc.v.mPositionU.w, sfc.v.mNormalV.w, 0);
	MaterialData md = gMaterials[NonUniformResourceIndex(sfc.instance.mMaterialIndex)];
	ImageIndices inds = unpack_image_indices(md.mImageIndices);
	if (inds.mAlbedo < gImageCount) md.mAlbedo *= gImages[NonUniformResourceIndex(inds.mAlbedo)].SampleLevel(gSampler, texcoord.xy, texcoord.z).rgb;
	if (inds.mMetallic < gImageCount)  md.mMetallic = saturate(md.mMetallic*gImages[NonUniformResourceIndex(inds.mMetallic)].SampleLevel(gSampler, texcoord.xy, texcoord.z)[inds.mMetallicChannel]);
	if (inds.mRoughness < gImageCount) md.mRoughness = saturate(md.mRoughness*gImages[NonUniformResourceIndex(inds.mRoughness)].SampleLevel(gSampler, texcoord.xy, texcoord.z)[inds.mRoughnessChannel]);
	if (inds.mEmission < gImageCount)  md.mEmission *= gImages[NonUniformResourceIndex(inds.mEmission)].SampleLevel(gSampler, texcoord.xy, texcoord.z).rgb;
	if (inds.mNormal < gImageCount) {
		float3 bump = gImages[NonUniformResourceIndex(inds.mNormal)].SampleLevel(gSampler, texcoord.xy, texcoord.z).xyz*2 - 1;
		bump.xy *= md.mNormalScale;
		sfc.v.mNormalV.xyz = normalize(bump.x*sfc.v.mTangent.xyz + bump.y*cross(sfc.v.mTangent.xyz, sfc.v.mNormalV.xyz) + bump.z*sfc.v.mNormalV.xyz);
	}
	// ortho-normalize
	sfc.v.mTangent.xyz = normalize(sfc.v.mTangent.xyz - sfc.v.mNormalV.xyz * dot(sfc.v.mNormalV.xyz, sfc.v.mTangent.xyz));
	return md;
}

float eval_environment(float3 omega_in, out float2 uv) {
	uint2 hdrResolution;
	uint mips;
	gImages[gEnvironmentMap].GetDimensions(0, hdrResolution.x, hdrResolution.y, mips);
	float theta = acos(clamp(omega_in.y, -1, 1));
	uv = float2(atan2(omega_in.z, omega_in.x)/M_PI *.5 + .5, theta / M_PI);
	float pdf = gEnvironmentConditionalDistribution.SampleLevel(gSampler, uv, 0).y * gEnvironmentMarginalDistribution.SampleLevel(gSampler, float2(uv.y, 0), 0).y;
	return (pdf * hdrResolution.x*hdrResolution.y) / (2 * M_PI * M_PI * sin(theta));
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


// this function performs the direct lighting calculation
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
					float misWeight = powerHeuristic(pdf_light, pdf_bsdf);
					Li += misWeight * f * abs(dot(omega_in, sfc.v.mNormalV.xyz)) * bg / pdf_light;
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

float3 trace_path(inout RandomSampler rng, RayDesc ray, out float3 primaryNormal, out float primaryT, out float3 primaryAlbedo, out uint primaryMeshId) {
	primaryNormal = ray.Direction;
	primaryT = 1.#INF;
	primaryMeshId = -1;
	primaryAlbedo = 1;

	float3 radiance = 0;
	float3 throughput = 1;
	bool isEmitter = false;
	float pdf_bsdf = 1;
	
	RayQuery<RAY_FLAG_NONE> rayQuery;
	for (uint bounceIndex = 0; bounceIndex < gMaxBounces && any(throughput > 1e-6); bounceIndex++) {
		
		rayQuery.TraceRayInline(gScene, RAY_FLAG_NONE, ~0, ray);
		if (!do_ray_query(rayQuery)) {
			if (gEnvironmentMap < gImageCount) {
				float2 uv;
				float pdf_bg = eval_environment(ray.Direction, uv);
				float3 bg = gImages[gEnvironmentMap].SampleLevel(gSampler, uv, 0).rgb;
				if (bounceIndex > 0)
					bg *= powerHeuristic(pdf_bsdf, pdf_bg);
				else {
					if (gDebugMode == DEBUG_PDF)
						return pdf_bg;
					else if (gDebugMode == DEBUG_BG_PDF) {
						float c = 0;
						for (uint i = 0; i < 128; i++) {
							float3 test;
							float pdf_test;
							sample_environment(rng, test, pdf_test);
							c += pow(saturate(dot(test,ray.Direction)), 200);
						}
						return c;
					}
				}

				radiance += bg * throughput;				
			}
			return radiance;
		}

		SurfaceData sfc = surface_attributes(rayQuery.CommittedInstanceID(), rayQuery.CommittedPrimitiveIndex(), rayQuery.CommittedTriangleBarycentrics());
		
		if (gDebugMode == DEBUG_NORMALS_SMOOTH) return sfc.v.mNormalV.xyz*.5 + .5;
		else if (gDebugMode == DEBUG_NORMALS_GEOMETRY) return sfc.Ng*.5 + .5;
		else if (gDebugMode == DEBUG_TANGENTS) return sfc.v.mTangent.xyz*.5 + .5;

		MaterialData material = material_attributes(sfc);

		if (bounceIndex == 0) {
			primaryT = rayQuery.CommittedRayT();
			primaryNormal = sfc.Ng;
			primaryAlbedo = material.mAlbedo;
			uint x = rayQuery.CommittedInstanceID();
			uint y = rayQuery.CommittedPrimitiveIndex();
			primaryMeshId = x ^ (y + 0x9e3779b9 + (x << 6) + (x >> 2));
		}
		
		if      (gDebugMode == DEBUG_ALBEDO) return material.mAlbedo;
		else if (gDebugMode == DEBUG_METALLIC) return material.mMetallic;
		else if (gDebugMode == DEBUG_ROUGHNESS) return material.mRoughness;
		else if (gDebugMode == DEBUG_EMISSION) return material.mEmission;
		else if (gDebugMode == DEBUG_NORMALS_BUMP) return sfc.v.mNormalV.xyz*.5 + .5;
		else if (gDebugMode == DEBUG_TANGENTS_BUMP) return sfc.v.mTangent.xyz*.5 + .5;

		radiance += throughput * material.mEmission;

		//if (any(material.mEmission > 0)) {
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
			throughput *= exp(-material.mAbsorption * rayQuery.CommittedRayT());
			sfc.v.mNormalV.xyz = -sfc.v.mNormalV.xyz;
			sfc.Ng = -sfc.Ng;
			disneyMat.eta = 1/disneyMat.eta;
		}

		// if (!bsdf.deltaSpecular)
		radiance += throughput * direct_light(rayQuery, rng, -ray.Direction, sfc, disneyMat);

		float3 omega_in;
		float3 f = DisneySample(rng, disneyMat, -ray.Direction, sfc.v.mNormalV.xyz, sfc.v.mTangent.xyz, omega_in, pdf_bsdf);

		if (pdf_bsdf <= 0)
				break;
				
		throughput *= f * abs(dot(sfc.v.mNormalV.xyz, omega_in)) / pdf_bsdf;

		// Russian roulette
		if (bounceIndex >= gRussianRouletteDepth) {
			float q = min(max(throughput.x, max(throughput.y, throughput.z)) + 0.001, 0.95);
			if (rng.next_sample() > q)
				break;
			throughput /= q;
		}

		ray.Direction = omega_in;
		ray.Origin = ray_offset(sfc.v.mPositionU.xyz, dot(omega_in, sfc.Ng) < 0 ? -sfc.Ng : sfc.Ng);
		ray.TMin = 0;
		ray.TMax = 1.#INF;
	}

	return radiance;
}

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gSamples.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy > resolution)) return;

	RandomSampler rng;
	rng.v = uint4(index.xy, gPushConstants.gRandomSeed, index.x + index.y);
	
	float2 uv = (float2(index.xy) + 0.5)/float2(resolution);
	float3 screenPos = float3(2*uv-1, 1);
	screenPos.y = -screenPos.y;
	float3 unprojected = back_project(gProjection, screenPos);
	RayDesc ray;
	ray.Direction = normalize(transform_vector(gCameraToWorld, float3(unprojected.xy, sign(gProjection.mNear))));
	ray.Origin = gCameraToWorld.mTranslation;
	ray.TMin = gProjection.mNear;
	ray.TMax = 1.#INF;

	float3 radiance = 0;
	float3 primaryAlbedo = 0;
	uint primaryMeshId;
	float primaryT = 0;
	float3 primaryNormal = 0;
	for (uint i = 0; i < gSampleCount; i++) {
		float t;
		float3 normal;
		float pdf_path;
		radiance += trace_path(rng, ray, normal, t, primaryAlbedo, primaryMeshId);
		primaryT += t;
		primaryNormal += normal;
	}
	radiance /= gSampleCount;
	primaryT /= gSampleCount;
	primaryNormal /= gSampleCount;
	primaryAlbedo /= gSampleCount;

	float4 prevPos = project_point(gPrevProjection, transform_point(inverse(gPrevCameraToWorld), ray.Origin + ray.Direction * primaryT));
	prevPos.xyz /= prevPos.w;
	prevPos.y = -prevPos.y;
	prevPos.xy = prevPos.xy*.5 + .5;

	if (gDemodulateAlbedo) {
		if (primaryAlbedo.x > 0) radiance.x /= primaryAlbedo.x;
		if (primaryAlbedo.y > 0) radiance.y /= primaryAlbedo.y;
		if (primaryAlbedo.z > 0) radiance.z /= primaryAlbedo.z;
	}
	
	gSamples[index.xy] = float4(radiance, gSampleCount);
	gZ[index.xy] = float2(primaryT, 1/abs(normalize(transform_vector(inverse(gCameraToWorld), primaryNormal))).z);
	gNormalId[index.xy] = float4(primaryNormal, asfloat(primaryMeshId));
	gAlbedo[index.xy] = float4(primaryAlbedo, 0);
	gPrevUV[index.xy] = prevPos.xy;
}