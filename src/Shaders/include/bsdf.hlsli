#ifndef BSDF_H
#define BSDF_H

#include "stratum.hlsli"

struct BSDF {
	float3 diffuse;
	float3 specular;
	float3 emission;
	float roughness;
};

static const float gDielectricSpecular = 0.04;

BSDF make_BSDF(float3 baseColor, float metallic, float roughness, float3 emission = 0) {
	BSDF bsdf;
	bsdf.specular = lerp(gDielectricSpecular, baseColor, metallic);
	bsdf.diffuse = baseColor * (1 - gDielectricSpecular.r) * (1 - metallic) / max(1 - max(max(bsdf.specular.r, bsdf.specular.g), bsdf.specular.b), 1e-6);
	bsdf.roughness = max(.002, roughness);
	bsdf.emission = emission;
	return bsdf;
}

float3 fresnel(float3 specular, float VdotH) {
  float3 f0 = specular;
  float3 f90 = 50*max(max(specular.r, specular.g), specular.b);
  return lerp(f0, f90, pow5(1 - VdotH));
}
float visibilityOcclusion(float NdotL, float NdotV, float alphaRoughnessSq) {
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGX = GGXV + GGXL;
	return GGX > 0 ? 0.5 / GGX : 0;
}
float microfacetDistribution(float NdotH, float alphaRoughnessSq) {
	return alphaRoughnessSq / (M_PI * pow2(1 + NdotH*(NdotH*alphaRoughnessSq - NdotH)) + 0.000001);
}

float3 Evaluate(BSDF bsdf, float3 Li, float3 Lo, float3 normal, float3 position) {
	float alphaRoughness = pow2(bsdf.roughness);
	float alphaRoughnessSq = pow2(alphaRoughness);
	float NdotL = saturate(dot(normal, Li));
	float NdotV = saturate(dot(normal, Lo));
	if (NdotL <= 0 || NdotV <= 0) return 0;
	float3 H = normalize(Li + Lo);
	float3 F = fresnel(bsdf.specular, saturate(dot(Lo, H)));
	float Vis = visibilityOcclusion(NdotL, NdotV, alphaRoughnessSq);
	float D = microfacetDistribution(saturate(dot(normal, H)), alphaRoughnessSq);
	float3 diffuseContrib = (1 - F) * (bsdf.diffuse / M_PI);
	float3 specContrib = F * Vis * D;
	return NdotL*(diffuseContrib + specContrib);
}

#endif