#pragma kernel Raytrace

#pragma array Textures 64
#pragma static_sampler Sampler maxAnisotropy=0 maxLod=0

#define EPSILON 0.001
#define MIN_ROUGHNESS 0.001
#define MAX_RADIANCE 5
#define ENVIRONMENT_TEXTURE_INDEX 0

#include "rtcommon.h"

[[vk::binding(0, 0)]] RWTexture2D<float4> Radiance								: register(u0);
[[vk::binding(1, 0)]] RWTexture2D<float4> Normals									: register(u2);
[[vk::binding(2, 0)]] RWTexture2D<float4> Positions								: register(u3);

[[vk::binding(4, 0)]] RWTexture2D<float4> RadianceHistory		    	: register(u4);
[[vk::binding(5, 0)]] RWTexture2D<float4> NormalsHistory		      : register(u5);
[[vk::binding(6, 0)]] RWTexture2D<float4> PositionsHistory		    : register(u5);

[[vk::binding(8, 0)]] StructuredBuffer<BvhNode> SceneBvh					: register(t0);
[[vk::binding(9, 0)]] ByteAddressBuffer Vertices									: register(t1);
[[vk::binding(10, 0)]] ByteAddressBuffer Triangles									: register(t2);
[[vk::binding(11, 0)]] StructuredBuffer<uint> PrimitiveMaterials 	: register(t3);

[[vk::binding(12, 0)]] StructuredBuffer<RTLight> Lights						: register(t4);
[[vk::binding(13, 0)]] StructuredBuffer<RTMaterial> Materials 		: register(t5);
[[vk::binding(14, 0)]] Texture2D<float4> Textures[TEXTURE_COUNT] 	: register(t6);
[[vk::binding(15, 0)]] SamplerState Sampler												: register(s0);

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
	float4 CameraRotation;
	float3 CameraPosition;
	float FieldOfView;

	float4 InvCameraRotationHistory;
	float4 CameraRotationHistory;
	float3 CameraPositionHistory;
	float FieldOfViewHistory;

	uint MaxSurfaceBounces;
	uint MaxVolumeBounces;
	uint RandomSeed;
	uint LightCount;

	float3 AmbientLight;
	float HistoryTrust;
	float2 Resolution;
	float2 ResolutionHistory;

	uint StereoEye;
	uint VertexStride;
};

#include "bvh.hlsli"
#include "bsdf.hlsli"

struct Volume {
	float3 sigma_t;
	float ior;
	float3 sigma_s;
	float pad;
};
struct Path {
	float3 throughput;
	float last_pdf;
	float3 radiance;
	uint surfaceBounceIndex;
	uint volumeBounceIndex;
	uint pad[3];
};

BSDF SampleSurface(inout RandomSampler rng, Intersection intersection, out float3x3 tangentToWorld, out float3 worldNormal, out float pdf) {
	RTMaterial m = Materials[intersection.MaterialIndex];

	float3 bump = float3(0, 0, 1);
	float3 bary = float3(intersection.Barycentrics, 1 - intersection.Barycentrics.x - intersection.Barycentrics.y);

	uint3 addr = VertexStride * Triangles.Load3(3 * 4 * intersection.PrimitiveIndex);
	addr += 12;
	float3 n0 = asfloat(Vertices.Load3(addr.x));
	float3 n1 = asfloat(Vertices.Load3(addr.y));
	float3 n2 = asfloat(Vertices.Load3(addr.z));
	addr += 12;
	float4 t0 = asfloat(Vertices.Load4(addr.x));
	float4 t1 = asfloat(Vertices.Load4(addr.y));
	float4 t2 = asfloat(Vertices.Load4(addr.z));
	addr += 16;
	if (m.BaseColorTexture > 0 || m.RoughnessTexture > 0 || m.NormalTexture > 0) {
		float2 uv0 = asfloat(Vertices.Load2(addr.x));
		float2 uv1 = asfloat(Vertices.Load2(addr.y));
		float2 uv2 = asfloat(Vertices.Load2(addr.z));
		float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
		uv = uv * m.TextureST.xy + m.TextureST.zw;
		if (m.BaseColorTexture > 0) m.BaseColor *= Textures[m.BaseColorTexture].SampleLevel(Sampler, uv, 0);
		if (m.RoughnessTexture > 0) m.Roughness *= Textures[m.RoughnessTexture].SampleLevel(Sampler, uv, 0).r;
		if (m.NormalTexture > 0) { bump = Textures[m.NormalTexture].SampleLevel(Sampler, uv, 0).rgb * 2 - 1; }
	}

 	worldNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
	float4 tangent = t0 * bary.x + t1 * bary.y + t2 * bary.z;
	float3 bitangent = cross(tangent.xyz, worldNormal) * tangent.w;

	worldNormal = normalize(bump.x * tangent.xyz + bump.y * bitangent + bump.z * worldNormal);

	// ortho-normalize
	tangent.xyz = normalize(tangent.xyz - worldNormal * dot(worldNormal, tangent.xyz));
	bitangent = cross(worldNormal, tangent.xyz) * tangent.w;
	
	tangentToWorld = float3x3(
		tangent.x, worldNormal.x, bitangent.x,
		tangent.y, worldNormal.y, bitangent.y,
		tangent.z, worldNormal.z, bitangent.z );

	BSDF bsdf = {};
	if (m.Emission.w > 0) {
		bsdf.Type = BSDF_TYPE_EMISSION;
		BSDF_EMISSION(bsdf) = m.Emission.rgb * m.Emission.w;
		pdf = 1;
		return bsdf;
	}

	float diffuseWeight = saturate(1 - m.Metallic) * saturate(1 - m.Transmission);
	float specularWeight = saturate(m.Metallic);
	float transmissionWeight = saturate(m.Transmission * (1 - m.Metallic));
	float totalWeight = diffuseWeight + specularWeight + transmissionWeight;
	float rnd = SampleRNG(rng).x * totalWeight;

	BSDF_ALPHA(bsdf) = sqr(saturate(max(MIN_ROUGHNESS, m.Roughness)));
	
	if (rnd < specularWeight + transmissionWeight) {
		bsdf.Type = BSDF_TYPE_MICROFACET;
		BSDF_MF_ALPHA_TRANSMISSION(bsdf) = sqr(saturate(max(MIN_ROUGHNESS, m.TransmissionRoughness)));
		BSDF_MF_IOR(bsdf) = m.IndexOfRefraction;
		BSDF_MF_SPECULAR(bsdf) = lerp(0.04, m.BaseColor, specularWeight);
		BSDF_MF_TRANSMISSION(bsdf) = transmissionWeight;
		BSDF_MF_SIGMA_T(bsdf) = m.Absorption + m.Scattering;
		BSDF_MF_SIGMA_S(bsdf) = m.Scattering;
		pdf = (specularWeight + transmissionWeight) / totalWeight;
		return bsdf;
	}
	rnd -= specularWeight + transmissionWeight;

	if (rnd < diffuseWeight) {
		bsdf.Type = BSDF_TYPE_DIFFUSE;
		BSDF_DIFFUSE(bsdf) = m.BaseColor;
		pdf = diffuseWeight / totalWeight;
		return bsdf;
	}
	rnd -= diffuseWeight;

	// shouldn't get here...
	BSDF_EMISSION(bsdf) = float3(1, 0, 1);
	bsdf.Type = BSDF_TYPE_EMISSION;
	pdf = 1;
	return bsdf;
}

float3 SampleBackground(float3 wi) {
	float2 uv = float2(atan2(wi.z, wi.x)*INV_PI*.5+.5, acos(wi.y) * INV_PI);
	return AmbientLight * Textures[ENVIRONMENT_TEXTURE_INDEX].SampleLevel(Sampler, uv, 0).rgb;
}
void SampleBackgroundImportance(inout RandomSampler rng, out float3 wi, out float pdf) {
	static const uint2 offsets[4] = {
		uint2(0,0),
		uint2(1,0),
		uint2(0,1),
		uint2(1,1),
	};

	pdf = 1;

	uint2 coord = 0;
 	uint2 resolution;
	uint mipCount;
	Textures[ENVIRONMENT_TEXTURE_INDEX].GetDimensions(0, resolution.x, resolution.y, mipCount);
	uint2 lastMipResolution = 1;
 	for (uint i = 1; i < min(8, mipCount); i++) {
		uint mipLevel = mipCount - 1 - i;
		uint tmp;
		uint2 mipResolution;
		Textures[ENVIRONMENT_TEXTURE_INDEX].GetDimensions(mipLevel, mipResolution.x, mipResolution.y, tmp);
		coord *= mipResolution/lastMipResolution;

		float4 p = 0;
		if (mipResolution.x > 1) {
			p[0] = dot(float3(0.3, 0.6, 1.0), Textures[ENVIRONMENT_TEXTURE_INDEX].Load(uint3(coord + offsets[0], mipLevel).rgb));
			p[1] = dot(float3(0.3, 0.6, 1.0), Textures[ENVIRONMENT_TEXTURE_INDEX].Load(uint3(coord + offsets[1], mipLevel).rgb));
		}
		if (mipResolution.y > 1) {
			p[2] = dot(float3(0.3, 0.6, 1.0), Textures[ENVIRONMENT_TEXTURE_INDEX].Load(uint3(coord + offsets[2], mipLevel).rgb));
			p[3] = dot(float3(0.3, 0.6, 1.0), Textures[ENVIRONMENT_TEXTURE_INDEX].Load(uint3(coord + offsets[3], mipLevel).rgb));
		}
		float sum = dot(p, 1);
		if (sum < EPSILON) continue;
		p /= sum;

		float rnd = SampleRNG(rng).x;
		for (uint j = 0; j < 4; j++) {
			if (rnd < p[j]) {
				coord += offsets[j];
				pdf *= p[j];
				break;
			}
			rnd -= p[j];
		}
		lastMipResolution = mipResolution;
	}
	
	float2 uv = (float2(coord) + SampleRNG(rng)) / float2(lastMipResolution);
	float theta = PI*(uv.x*2 - 1);
	float phi = uv.y * PI;
	float sinPhi = sin(phi);
	wi = float3(sinPhi*cos(theta), cos(phi), sinPhi*sin(theta));
}
float3 SampleLight(inout RandomSampler rng, float3 worldPos, out float3 wi, out float lightDistance, out float pdf) {
	uint lightIndex = clamp(uint(SampleRNG(rng).x * (LightCount + 1)), 0, LightCount);
	if (lightIndex == 0) {
		lightDistance = 1.#INF;
		SampleBackgroundImportance(rng, wi, pdf);
		pdf /= (float)LightCount + 1;
		return SampleBackground(wi);
	}
	lightIndex--;
	RTLight light = Lights[lightIndex];

	uint3 addr = VertexStride * Triangles.Load3(4 * 3 * light.PrimitiveIndex);
	float3 v0 = asfloat(Vertices.Load3(addr.x));
	float3 v1 = asfloat(Vertices.Load3(addr.y));
	float3 v2 = asfloat(Vertices.Load3(addr.z));

	float2 bary = SampleRNG(rng);
	if (bary.x + bary.y >= 1) bary = 1 - bary;

	float3 v1v0 = v1 - v0;
	float3 v2v0 = v2 - v0;

	float3 lightPos = v0 + v1v0 * bary.x + v2v0 * bary.y;
	float3 lightNormal = cross(v2v0, v1v0);
	float area = length(lightNormal);
	lightNormal /= area;
	area /= 2;

	wi = lightPos - worldPos;
	lightDistance = length(wi);
	wi /= lightDistance;

	float lnv = dot(wi, -lightNormal);
	if (lnv <= 0 || area <= EPSILON) { pdf = 0; return 0; }
	
	pdf = lightDistance*lightDistance / (lnv * area + (float)LightCount + 1);
	return Materials[PrimitiveMaterials[light.PrimitiveIndex]].Emission;
}

float3 VolumeTransmittance(Volume volume, float t) {
	if (t > 1e12) return volume.sigma_t == 0;
	return exp(-volume.sigma_t * t);
}
bool VolumeScatter(inout RandomSampler rng, inout Path path, Volume volume, inout Ray ray, float t) {
	if (all(volume.sigma_t < EPSILON)) return false;

	float3 pdf_channel = path.throughput * volume.sigma_s/volume.sigma_t;
	float sum = dot(pdf_channel, 1.0);
	if (sum < EPSILON) pdf_channel = 1.0/3.0;
	else pdf_channel /= sum;

	int channel;
	float rnd = SampleRNG(rng).x;
	if (rnd.x < pdf_channel.r) channel = 0;
	else if (rnd.x < pdf_channel.r + pdf_channel.g) channel = 1;
	else channel = 2;

	float3 Tr = VolumeTransmittance(volume, t);
	
	if (SampleRNG(rng).x > pdf_channel[channel]) {
		// Scatter ray within volume
		t = min(t - EPSILON, t * -log(1 - SampleRNG(rng).x * (1 - Tr[channel])) / volume.sigma_t[channel]);

		float3 Trt = VolumeTransmittance(volume, t);
		float3 pdf_t = volume.sigma_t*Trt;

		float3 worldPos = ray.Origin + ray.Direction * t;
		float3 wi = Sample_MapToSphere(SampleRNG(rng));
		float pdf_wi = 1 / (4*PI);
		//float pdf_wi, lightDistance;
		//SampleLight(rng, worldPos, wi, lightDistance, pdf_wi);

		path.last_pdf = pdf_wi * dot(pdf_channel, pdf_t) * (1 - pdf_channel[channel]);
		path.throughput *= volume.sigma_s * Trt / path.last_pdf;
		ray = CreateRay(worldPos, wi, 0, 1.#INF);
		return true;
	} else {
		// Transmit ray through volume
		path.last_pdf = pdf_channel[channel];
		path.throughput *= Tr / path.last_pdf;
		return false;
	}
}

float3 SampleLightDirect(inout RandomSampler rng, BSDF bsdf, Volume medium, float3x3 tangentToWorld, float3 wo_t, float3 worldPos, float3 worldNormal) {
	float3 wi;
	float pdf_le, light_t;
	float3 Le = SampleLight(rng, worldPos, wi, light_t, pdf_le);
	Le *= VolumeTransmittance(medium, light_t);
	if (pdf_le <= 0) return 0;
	pdf_le /= abs(dot(wi, worldNormal));
	
	float pdf_transport;
	float3 transport = EvaluateBSDF(bsdf, mul(wi, tangentToWorld), wo_t, pdf_transport);
	if (all(transport < 0.0001)) return 0;

	Intersection intersection;
	if (IntersectScene(CreateRay(worldPos, wi, EPSILON, light_t - EPSILON), false, intersection)) return 0;

	return Le * transport / pdf_le * BalanceHeuristic(pdf_le, pdf_transport);
}

float4 AccumulateHistory(float2 histUV, float aspect, float3 worldPos, float3 worldNormal) {
  if (any(histUV < 0) || any(histUV > 1)) return 0;
	
	float3 view = worldPos - CameraPosition;
	float depth = length(view);
	view /= depth;

	int2 histCoord = clamp(int2(histUV * ResolutionHistory), 0, int2(ResolutionHistory) - 1);

	float3 sampleNormal = NormalsHistory[histCoord].xyz;
	float3 samplePos = PositionsHistory[histCoord].xyz;
	
	float3 sampleView = samplePos - CameraPosition;
	float sampleDepth = length(sampleView);
	sampleView /= sampleDepth;

	float depthSimilarity = length(1 - sampleDepth/depth);
	float normalSimilarity = sqr(saturate(dot(sampleNormal.xyz, worldNormal)));
	float viewSimilarity = sqr(saturate(dot(sampleView, view)));
	
	if (depthSimilarity < 0.0005 && normalSimilarity > 0.9995 && viewSimilarity > 0.9995)
		return RadianceHistory[histCoord];
	return 0;
}

float3 TracePath(inout RandomSampler rng, Ray ray, out float3 hitNormal, out float3 hitPos) {
	Intersection intersection, lightIntersection;
	BSDF bsdf;
	float3x3 tangentToWorld;
	float3 worldNormal;
	float pdf_surface;

	hitPos = ray.Origin;
	hitNormal = 0;

	Volume medium = {};
	medium.sigma_s = 0;
	medium.sigma_t = 0;
	medium.ior = 1;

	Path path = {};
	path.last_pdf = 1;
	path.radiance = 0;
	path.throughput = 1;
	path.surfaceBounceIndex = 0;
	path.volumeBounceIndex = 0;

	while (path.surfaceBounceIndex < MaxSurfaceBounces && any(path.throughput > 0.001)) {
		if (!IntersectScene(ray, false, intersection)) {
			path.throughput *= VolumeTransmittance(medium, ray.TMax) * SampleBackground(ray.Direction);
			path.radiance += path.throughput;
			break;
		}

		if (any(medium.sigma_s > 0) && path.volumeBounceIndex < MaxVolumeBounces) {
			if (VolumeScatter(rng, path, medium, ray, intersection.HitT.x)) {
				path.last_pdf = 1;
				path.volumeBounceIndex++;
				continue;
			}
		} else
			path.throughput *= VolumeTransmittance(medium, intersection.HitT.x);

		bsdf = SampleSurface(rng, intersection, tangentToWorld, worldNormal, pdf_surface);
		float3 wo_t = mul(-ray.Direction, tangentToWorld);

		// record geometry of first hit
		float3 worldPos = ray.Origin + ray.Direction * intersection.HitT.x;
		if (path.volumeBounceIndex == 0 && path.surfaceBounceIndex == 0) { hitPos = worldPos; hitNormal = worldNormal; }

		// terminate on light source
		if (bsdf.Type == BSDF_TYPE_EMISSION) {
			if (wo_t.y <= 0) break;
			float mis_weight = (path.last_pdf < 1) ? BalanceHeuristic(path.last_pdf, sqr(intersection.HitT.x) / (wo_t.y*intersection.Area)) : 1;
			path.radiance += path.throughput * BSDF_EMISSION(bsdf) * mis_weight;
			break;
		}

		path.radiance += path.throughput * SampleLightDirect(rng, bsdf, medium, tangentToWorld, wo_t, worldPos, worldNormal);

		// Continue path trace
		float3 wi_t;
		path.throughput *= SampleBSDF(rng, bsdf, wo_t, wi_t, path.last_pdf);
		ray = CreateRay(worldPos, mul(tangentToWorld, wi_t), EPSILON, 1.#INF);
		path.surfaceBounceIndex++;

		// Handle any changes in medium
		if (wi_t.y < 0 && wo_t.y > 0) {
			// Entered surface
			medium.sigma_t = BSDF_MF_SIGMA_T(bsdf) + BSDF_MF_SIGMA_S(bsdf);
			medium.sigma_s = BSDF_MF_SIGMA_S(bsdf);
			medium.ior = BSDF_MF_IOR(bsdf);
		} else if (wi_t.y > 0 && wo_t.y < 0) {
			// Exited surface
			medium.sigma_s = 0;
			medium.sigma_t = 0;
			medium.ior = 1;
		}
	}
	return path.radiance;
}

[numthreads(8, 8, 1)]
void Raytrace(uint3 index : SV_DispatchThreadID) {
	uint2 ires = uint2(Resolution);
	
	uint idx = index.y * uint2(Resolution).x + index.x;
	RandomSampler rng;
	rng.index = RandomSeed % (CMJ_DIM * CMJ_DIM);
	rng.dimension = 1;
	rng.scramble = idx * 0x1fe3434f * ((RandomSeed + 133 * idx) / (CMJ_DIM * CMJ_DIM));

	float aspect = Resolution.x / Resolution.y;
	float2 screenSize = tan(.5 * FieldOfView) * float2(aspect, 1);

	float2 uv = (float2(index.xy) + 0.5) / Resolution;
  float3 viewRay = float3(screenSize * (uv*2-1), 1);
	viewRay.y = -viewRay.y;
	Ray ray = CreateRay(CameraPosition, normalize(qtRotate(CameraRotation, viewRay)), 0, 1.#INF);

	float3 worldNormal;
	float3 worldPos;
	float4 radiance = float4(TracePath(rng, ray, worldNormal, worldPos), 1);

	if (HistoryTrust > 0) {
		float4 radianceHist = AccumulateHistory(uv, aspect, worldPos, worldNormal);
		if (radianceHist.w == radianceHist.w && radianceHist.w > EPSILON) {
			radianceHist *= HistoryTrust;
			radiance = float4(radiance + radianceHist.rgb * radianceHist.w, HistoryTrust*radianceHist.w + 1);
			if (radiance.w > EPSILON) radiance.rgb /= radiance.w;
		}
	}

	Radiance[index.xy] = radiance;
	Normals[index.xy] = float4(worldNormal, 1);
	Positions[index.xy] = float4(worldPos, 1);
}