/*
 * MIT License
 *
 * Copyright(c) 2019-2021 Asif Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files(the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions :
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 /* References:
 * [1] https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
 * [2] https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
 * [3] https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
 * [4] https://github.com/mmacklin/tinsel/blob/master/src/disney.h
 * [5] http://simon-kallweit.me/rendercompo2015/report/
 * [6] http://shihchinw.github.io/2015/07/implementing-disney-principled-brdf-in-arnold.html
 * [7] https://github.com/mmp/pbrt-v4/blob/0ec29d1ec8754bddd9d667f0e80c4ff025c900ce/src/pbrt/bxdfs.cpp#L76-L286
 * [8] https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
 */

// https://github.com/knightcrawler25/GLSL-PathTracer/blob/master/src/shaders/common/disney.glsl

#include "sampling.hlsli"

struct DisneyMaterial {
    float3 albedo;
    float specular;
    float metallic;
    float roughness;
    float subsurface;
    float specularTint;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    float specTrans;
    float eta;
};

float3 EvalDielectricReflection(DisneyMaterial mat, float3 V, float3 N, float3 L, float3 H, out float pdf) {
    pdf = 0;
    if (dot(N, L) <= 0)
		return 0;

    float F = DielectricFresnel(dot(V, H), mat.eta);
    float D = GTR2(dot(N, H), mat.roughness);
    
    pdf = D * dot(N, H) * F / (4 * abs(dot(V, H)));

    float G = SmithG_GGX(abs(dot(N, L)), mat.roughness) * SmithG_GGX(abs(dot(N, V)), mat.roughness);
    return mat.albedo * F * D * G;
}

float3 EvalDielectricRefraction(DisneyMaterial mat, float3 V, float3 N, float3 L, float3 H, out float pdf) {
    pdf = 0;
    if (dot(N, L) >= 0)
        return 0;

    float F = DielectricFresnel(abs(dot(V, H)), mat.eta);
    float D = GTR2(dot(N, H), mat.roughness);

    float denomSqrt = dot(L, H) + dot(V, H) * mat.eta;
    pdf = D * dot(N, H) * (1.0 - F) * abs(dot(L, H)) / (denomSqrt * denomSqrt);

    float G = SmithG_GGX(abs(dot(N, L)), mat.roughness) * SmithG_GGX(abs(dot(N, V)), mat.roughness);
    return mat.albedo * (1.0 - F) * D * G * abs(dot(V, H)) * abs(dot(L, H)) * 4.0 * mat.eta * mat.eta / (denomSqrt * denomSqrt);
}

float3 EvalSpecular(DisneyMaterial mat, float3 Cspec0, float3 V, float3 N, float3 L, float3 H, out float pdf) {
    pdf = 0;
    if (dot(N, L) <= 0)
        return 0;

    float D = GTR2(dot(N, H), mat.roughness);
    pdf = D * dot(N, H) / (4 * dot(V, H));

    float FH = SchlickFresnel(dot(L, H));
    float3 F = lerp(Cspec0, 1, FH);
    float G = SmithG_GGX(abs(dot(N, L)), mat.roughness) * SmithG_GGX(abs(dot(N, V)), mat.roughness);
    return F * D * G;
}

float3 EvalClearcoat(DisneyMaterial mat, float3 V, float3 N, float3 L, float3 H, out float pdf) {
    pdf = 0.0;
    if (dot(N, L) <= 0.0)
        return 0;

    float D = GTR1(dot(N, H), lerp(0.1, 0.001, mat.clearcoatGloss));
    pdf = D * dot(N, H) / (4.0 * dot(V, H));

    float FH = SchlickFresnel(dot(L, H));
    float F = lerp(0.04, 1.0, FH);
    float G = SmithG_GGX(dot(N, L), 0.25) * SmithG_GGX(dot(N, V), 0.25);
    return 0.25 * mat.clearcoat * F * D * G;
}

float3 EvalDiffuse(DisneyMaterial mat, float3 Csheen, float3 V, float3 N, float3 L, float3 H, out float pdf) {
    pdf = 0.0;
    if (dot(N, L) <= 0.0)
        return 0;

    pdf = dot(N, L) * (1.0 / M_PI);

    // Diffuse
    float FL = SchlickFresnel(dot(N, L));
    float FV = SchlickFresnel(dot(N, V));
    float FH = SchlickFresnel(dot(L, H));
    float Fd90 = 0.5 + 2.0 * dot(L, H) * dot(L, H) * mat.roughness;
    float Fd = lerp(1.0, Fd90, FL) * lerp(1.0, Fd90, FV);

    // Fake Subsurface TODO: Replace with volumetric scattering
    float Fss90 = dot(L, H) * dot(L, H) * mat.roughness;
    float Fss = lerp(1.0, Fss90, FL) * lerp(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (dot(N, L) + dot(N, V)) - 0.5) + 0.5);

    float3 Fsheen = FH * mat.sheen * Csheen;
    return ((1 / M_PI) * lerp(Fd, ss, mat.subsurface) * mat.albedo + Fsheen) * (1.0 - mat.metallic);
}

float3 DisneySample(inout RandomSampler rng, DisneyMaterial mat, float3 V, float3 N, float3 T, out float3 L, out float pdf) {
    pdf = 0;
    float3 f = 0;
    
    float3 B = cross(T,N);

    float r1 = rng.next_sample();
    float r2 = rng.next_sample();

    float diffuseRatio = 0.5 * (1 - mat.metallic);
    float transWeight = (1 - mat.metallic) * mat.specTrans;

    float3 Cdlin = mat.albedo;
    float Cdlum = 0.3 * Cdlin.x + 0.6 * Cdlin.y + 0.1 * Cdlin.z; // luminance approx.

    float3 Ctint = Cdlum > 0 ? Cdlin / Cdlum : 1; // normalize lum. to isolate hue+sat
    float3 Cspec0 = lerp(mat.specular * 0.08 * lerp(1, Ctint, mat.specularTint), Cdlin, mat.metallic);
    float3 Csheen = lerp(1, Ctint, mat.sheenTint);

    // TODO: Reuse random numbers and reduce so many calls to rand()
    if (rng.next_sample() < transWeight) {
        float3 H = ImportanceSampleGTR2(mat.roughness, r1, r2);
        H = T * H.x + B * H.y + N * H.z;

        if (dot(V, H) < 0)
            H = -H;

        float3 R = reflect(-V, H);
        float F = DielectricFresnel(abs(dot(R, H)), mat.eta);

        // Reflection/Total internal reflection
        if (rng.next_sample() < F) {
            L = normalize(R);
            f = EvalDielectricReflection(mat, V, N, L, H, pdf);
        } else { // Transmission
            L = normalize(refract(-V, H, mat.eta));
            f = EvalDielectricRefraction(mat, V, N, L, H, pdf);
        }

        f *= transWeight;
        pdf *= transWeight;
    } else {
        if (rng.next_sample() < diffuseRatio) { 
            L = CosineSampleHemisphere(r1, r2);
            L = T * L.x + B * L.y + N * L.z;

            float3 H = normalize(L + V);

            f = EvalDiffuse(mat, Csheen, V, N, L, H, pdf);
            pdf *= diffuseRatio;
        } else { // Specular
        
            float primarySpecRatio = 1.0 / (1.0 + mat.clearcoat);
            
            // Sample primary specular lobe
            if (rng.next_sample() < primarySpecRatio) {
                // TODO: Implement http://jcgt.org/published/0007/04/01/
                float3 H = ImportanceSampleGTR2(mat.roughness, r1, r2);
                H = T * H.x + B * H.y + N * H.z;

                if (dot(V, H) < 0.0)
                    H = -H;

                L = normalize(reflect(-V, H));

                f = EvalSpecular(mat, Cspec0, V, N, L, H, pdf);
                pdf *= primarySpecRatio * (1.0 - diffuseRatio);
            } else {
				// Sample clearcoat lobe
            
				float3 H = ImportanceSampleGTR1(lerp(0.1, 0.001, mat.clearcoatGloss), r1, r2);
				H = T * H.x + B * H.y + N * H.z;

				if (dot(V, H) < 0)
					H = -H;

				L = normalize(reflect(-V, H));

				f = EvalClearcoat(mat, V, N, L, H, pdf);
				pdf *= (1 - primarySpecRatio) * (1 - diffuseRatio);
            }
        }

        f *= (1 - transWeight);
        pdf *= (1 - transWeight);
    }
    return f;
}

float3 DisneyEval(DisneyMaterial mat, float3 V, float3 N, float3 L, out float pdf) {
    float3 H;
    bool refl = dot(N, L) > 0.0;

    if (refl)
      H = normalize(L + V);
    else
      H = normalize(L + V * mat.eta);

    if (dot(V, H) < 0.0)
      H = -H;

    float diffuseRatio = 0.5 * (1 - mat.metallic);
    float primarySpecRatio = 1 / (1 + mat.clearcoat);
    float transWeight = (1 - mat.metallic) * mat.specTrans;

    float3 brdf = 0;
    float3 bsdf = 0;
    float brdfPdf = 0;
    float bsdfPdf = 0;

    if (transWeight > 0) {
      // Reflection
      if (refl)
          bsdf = EvalDielectricReflection(mat, V, N, L, H, bsdfPdf); 
      else // Transmission
          bsdf = EvalDielectricRefraction(mat, V, N, L, H, bsdfPdf);
    }

    float m_pdf;

    if (transWeight < 1) {
      float3 Cdlin = mat.albedo;
      float Cdlum = 0.3 * Cdlin.x + 0.6 * Cdlin.y + 0.1 * Cdlin.z; // luminance approx.

      float3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : 1; // normalize lum. to isolate hue+sat
      float3 Cspec0 = lerp(mat.specular * 0.08 * lerp(1, Ctint, mat.specularTint), Cdlin, mat.metallic);
      float3 Csheen = lerp(1, Ctint, mat.sheenTint);

      // Diffuse
      brdf += EvalDiffuse(mat, Csheen, V, N, L, H, m_pdf);
      brdfPdf += m_pdf * diffuseRatio;
          
      // Specular
      brdf += EvalSpecular(mat, Cspec0, V, N, L, H, m_pdf);
      brdfPdf += m_pdf * primarySpecRatio * (1.0 - diffuseRatio);
          
      // Clearcoat
      brdf += EvalClearcoat(mat, V, N, L, H, m_pdf);
      brdfPdf += m_pdf * (1.0 - primarySpecRatio) * (1.0 - diffuseRatio);  
    }

    pdf = lerp(brdfPdf, bsdfPdf, transWeight);
    return lerp(brdf, bsdf, transWeight);
}