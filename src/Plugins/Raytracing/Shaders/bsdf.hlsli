#include "rtcommon.h"
#include <include/sampling.hlsli>

#define BSDF_TYPE_DIFFUSE 0
#define BSDF_TYPE_EMISSION 1
#define BSDF_TYPE_MICROFACET 2

struct BSDF {
    uint Type;
    float4 Data[4];
};

float BalanceHeuristic(float f, float g) { return f / (f + g); }

float G1(float alpha2, float cosTheta/*abs(dot(n,v))*/) {
    float tanTheta2 = sqr(1/cosTheta) - 1;
    return 2 / (1 + sqrt(1 + alpha2*tanTheta2));
}
float GGX(float alpha2, float cosTheta/*abs(dot(n,m))*/) {
    float tanTheta2 = sqr(1/cosTheta) - 1;
    return alpha2 / (PI * pow4(cosTheta) * sqr(alpha2 + tanTheta2));
}
// assumes i and n are in same hemisphere
float FresnelDialectric(float eta, float3 i, float3 n, out float3 reflection, out float3 refraction) {
  float cosTheta = dot(i, n);
  float neta = 1/eta;
  reflection = 2 * cosTheta * n - i;

  float k = 1 - (sqr(neta) * (1 - sqr(cosTheta)));
  if (k < 0) {
    refraction = 0;
    return 1; // total internal reflection
  } else {
    refraction = -neta * i + (neta * cosTheta - sqrt(k)) * n;
    float cosTheta1 = cosTheta;
    float cosTheta2 = -dot(n, refraction);
    float Rp = (cosTheta1 - eta * cosTheta2) / (cosTheta1 + eta * cosTheta2);
    float Rs = (eta * cosTheta1 - cosTheta2) / (eta * cosTheta1 + cosTheta2);
    return (sqr(Rp) + sqr(Rs)) / 2;
  }
}


float3 SampleGGX(inout RandomSampler rng, float alpha) {
    float2 s = SampleRNG(rng);
	float theta = atan(alpha * sqrt(s.x) / sqrt(1 - s.x));
	float cosTheta = cos(theta);
	float cosTheta2 = sqr(cosTheta);
	float sinTheta = sqrt(1 - cosTheta2);
	float phi = s.y * 2 * PI;
	return float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

float3 EvalDiffuseBSDF(float3 diffuse, float sigma, float3 wi, float3 wo, out float pdf) {
    if (sign(wo.y) != sign(wi.y)) { pdf = 0; return 0; };
    pdf = abs(wi.y) / PI;
    float sigma2 = sigma*sigma;
    float A = 1 - 0.5 * sigma2 / (sigma2 + 0.33);
    float B = 0.45 * sigma2 / (sigma2 + 0.09);
    float theta_i = acos(abs(wi.y));
    float theta_o = acos(abs(wo.y));
    float alpha = max(theta_i, theta_o); 
    float beta = min(theta_i, theta_o); 
    return diffuse / PI * abs(wi.y) * (A + (B * max(0, cos(theta_i - theta_o))*sin(alpha)*tan(beta) ));
}
float3 SampleDiffuseBSDF(inout RandomSampler rng, float3 diffuse, float sigma,float3 wo, out float3 wi, out float pdf) {
    wi = SampleHemisphereCosine(SampleRNG(rng)) * sign(wo.y);
    pdf = abs(wi.y) / PI;
    float tmp;
    return EvalDiffuseBSDF(diffuse, sigma, wi, wo, tmp) * abs(wi.y) / pdf;
}

// https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
float3 EvalMicrofacetBSDF(float3 specular, float transmission, float alpha_r, float alpha_t, float eta, float3 wi, float3 wo, out float pdf) {
    float3 m;
    if (sign(wi.y) == sign(wo.y)) {
        m = wi + wo;
    } else {
        if (transmission < EPSILON) { pdf = 0; return 0; }
        m = -(eta * wi + wo);
    }
    m = normalize(m);

    float mdotwi = dot(m, wi);
    float mdotwo = dot(m, wo);

    float3 wo_r, wo_t;
    float fresnel = FresnelDialectric(eta, wi, m, wo_r, wo_t);
    float3 F = lerp(specular, 1, fresnel);

    float pdf_transmit = transmission * (1 - fresnel);
    float alpha_ggx = 1 - (1 - alpha_r) * (1 - alpha_t*transmission);
    pdf = alpha_ggx < MIN_ROUGHNESS ? 1 : GGX(sqr(alpha_ggx), m.y)*abs(m.y) / (4 * abs(mdotwo)) * (1 - pdf_transmit);

    if (sign(wi.y) == sign(wo.y)) {
        // Reflection
        float alpha_r2 = alpha_r*alpha_r;
        float Dr = GGX(alpha_r2, m.y);
        float Gor = G1(alpha_r2, wo.y);
        float Gr = G1(alpha_r2, wi.y) * Gor;
        float c = Dr / abs(4*wo.y);
        pdf = (1 - pdf_transmit) * Gor * c;
        return F * Gr * c;
    } else {
        // Transmission
        float alpha_t2 = alpha_t*alpha_t;
        float Dt = GGX(alpha_t2, m.y);
        float Got = G1(alpha_t2, wo.y);
        float Gt = G1(alpha_t2, wi.y) * Got;
        float c = Dt * sqr(eta) / (wo.y * sqr(eta*mdotwi + mdotwo));
        pdf = pdf_transmit * Got * mdotwo * abs(mdotwi) * c;
        return transmission*(1-F)*Gt * abs(mdotwi * mdotwo) * c;
    }
}

float3 SampleMicrofacetBSDF(inout RandomSampler rng, float3 specular, float transmission, float alpha_r, float alpha_t, float eta, float3 wo, out float3 wi, out float pdf) {
    float alpha_ggx = 1 - (1 - alpha_r) * (1 - alpha_t*transmission);
    float3 m = SampleGGX(rng, alpha_ggx);
    float mdotwo = dot(m, wo);
    if (mdotwo < 0) { m = -m; mdotwo = -mdotwo; }

    float3 wi_r, wi_t;
    float fresnel = FresnelDialectric(eta, wo, m, wi_r, wi_t);
    float3 F = lerp(specular, 1, fresnel);
    float pdf_transmit = transmission * (1 - fresnel);

    if (SampleRNG(rng).x >= pdf_transmit) {
        // Reflection
        wi = wi_r;
        float alpha_r2 = alpha_r*alpha_r;
        float Dr = GGX(alpha_r2, m.y);
        float Gor = G1(alpha_r2, wo.y);
        float Gr = G1(alpha_r2, wi.y) * Gor;
        float c = Dr / abs(4*wo.y);
        pdf = (1 - pdf_transmit) * Gor * c;
        return F * Gr * c / pdf;
    } else {
        // Transmission
        wi = wi_t;
        float mdotwi = dot(m, wi);    
        float alpha_t2 = alpha_t*alpha_t;
        float Dt = GGX(alpha_t2, m.y);
        float Got = G1(alpha_t2, wo.y);
        float Gt = G1(alpha_t2, wi.y) * Got;
        float c = Dt * sqr(eta) / (wo.y * sqr(eta*mdotwi + mdotwo));
        pdf = pdf_transmit * Got * mdotwo * abs(mdotwi) * c;
        return transmission*(1-F)*Gt * abs(mdotwi * mdotwo) * c / pdf;
    }
}

#define BSDF_DIFFUSE(bsdf)                bsdf.Data[0].rgb
#define BSDF_EMISSION(bsdf)               bsdf.Data[0].rgb
#define BSDF_ALPHA(bsdf)                  bsdf.Data[0].x

#define BSDF_MF_ALPHA_TRANSMISSION(bsdf)  bsdf.Data[0].y
#define BSDF_MF_IOR(bsdf)                 bsdf.Data[0].z
#define BSDF_MF_TRANSMISSION(bsdf)        bsdf.Data[0].w
#define BSDF_MF_SPECULAR(bsdf)            bsdf.Data[1].rgb
#define BSDF_MF_SIGMA_T(bsdf)             bsdf.Data[2].rgb
#define BSDF_MF_SIGMA_S(bsdf)             bsdf.Data[3].rgb

float3 EvaluateBSDF(BSDF bsdf, float3 wi, float3 wo, out float pdf) {
    float3 eval = 0;
    pdf = 0;
    switch (bsdf.Type) {
        case BSDF_TYPE_MICROFACET:
            float eta = BSDF_MF_IOR(bsdf);
            if (wi.y < 0) eta = 1/eta;
            eval = EvalMicrofacetBSDF(BSDF_MF_SPECULAR(bsdf), BSDF_MF_TRANSMISSION(bsdf), BSDF_ALPHA(bsdf), BSDF_MF_ALPHA_TRANSMISSION(bsdf), eta, wi, wo, pdf);
            break;
        case BSDF_TYPE_DIFFUSE:
            eval = EvalDiffuseBSDF(BSDF_DIFFUSE(bsdf), BSDF_ALPHA(bsdf), wi, wo, pdf);
            break;
        case BSDF_TYPE_EMISSION:
            pdf = 0;
            eval = BSDF_EMISSION(bsdf);
            break;
    }
    return eval;
}
float3 SampleBSDF(inout RandomSampler rng, BSDF bsdf, float3 wo, out float3 wi, out float pdf) {
    float3 eval = 0;
    wi = 0;
    pdf = 0;
    switch (bsdf.Type) {
        case BSDF_TYPE_MICROFACET:
            float eta = BSDF_MF_IOR(bsdf);
            if (wo.y < 0) eta = 1/eta;
            eval = SampleMicrofacetBSDF(rng, BSDF_MF_SPECULAR(bsdf), BSDF_MF_TRANSMISSION(bsdf), BSDF_ALPHA(bsdf), BSDF_MF_ALPHA_TRANSMISSION(bsdf), eta, wo, wi, pdf);
            break;
        case BSDF_TYPE_DIFFUSE:
            eval = SampleDiffuseBSDF(rng, BSDF_DIFFUSE(bsdf), BSDF_ALPHA(bsdf), wo, wi, pdf);
            break;
        case BSDF_TYPE_EMISSION:
            pdf = 0;
            wi = 0;
            eval = BSDF_EMISSION(bsdf);
            break;
    }
    return eval;
}