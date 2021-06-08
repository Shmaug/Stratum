#ifndef BSDF_H
#define BSDF_H

#include "math.hlsli"

struct BSDF {
	float3 diffuse;
	float3 specular;
	float3 emission;
	float roughness;
};

static const float gDielectricSpecular = 0.04f;

inline BSDF make_BSDF(float3 baseColor, float metallic, float roughness, float3 emission = 0) {
	BSDF bsdf;
	bsdf.specular = lerp(gDielectricSpecular, baseColor, metallic);
	bsdf.diffuse = baseColor * (1 - gDielectricSpecular) * (1 - metallic) / max(1 - max3(bsdf.specular), 1e-6f);
	bsdf.roughness = max(.002f, roughness);
	bsdf.emission = emission;
	return bsdf;
}

inline float3 fresnel(float3 specular, float VdotH) {
  return lerp(specular, 50*max3(specular), pow5(1 - VdotH));
}
inline float visibilityOcclusion(float NdotL, float NdotV, float alphaRoughnessSq) {
	float GGXV = NdotL * sqrt(NdotV * NdotV * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGXL = NdotV * sqrt(NdotL * NdotL * (1 - alphaRoughnessSq) + alphaRoughnessSq);
	float GGX = GGXV + GGXL;
	return GGX > 0 ? 0.5 / GGX : 0;
}
inline float microfacetDistribution(float NdotH, float alphaRoughnessSq) {
	return alphaRoughnessSq / (M_PI * pow2(1 + NdotH*(NdotH*alphaRoughnessSq - NdotH)) + 0.000001);
}

inline float3 ShadePoint(BSDF bsdf, float3 Li, float3 Lo, float3 normal) {
	float alphaRoughness = pow2(bsdf.roughness);
	float alphaRoughnessSq = pow2(alphaRoughness);
	float NdotL = saturate(dot(normal, Li));
	float NdotV = saturate(dot(normal, Lo));
	if (NdotL <= 0 || NdotV <= 0) return 0;
	float3 H = normalize(Li + Lo);
	float3 F = fresnel(bsdf.specular, saturate(dot(Lo, H)));
	float Vis = visibilityOcclusion(NdotL, NdotV, alphaRoughnessSq);
	float D = microfacetDistribution(saturate(dot(normal, H)), alphaRoughnessSq);
	float3 diffuseContrib = (1 - F) * (bsdf.diffuse/M_PI);
	float3 specContrib = F * Vis * D;
	return NdotL*(diffuseContrib + specContrib);
}

#endif