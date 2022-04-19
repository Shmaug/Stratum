#ifndef ROUGHDIELECTRIC_H
#define ROUGHDIELECTRIC_H

#include "../scene.hlsli"
#ifdef __HLSL_VERSION
#include "../microfacet.hlsli"
#endif

struct RoughDielectric {
#ifdef __cplusplus
    ImageValue3 specular_reflectance;
    ImageValue3 specular_transmittance;
    ImageValue1 roughness;
    float eta;

    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        specular_transmittance.store(bytes, resources);
        specular_reflectance.store(bytes, resources);
        roughness.store(bytes, resources);
        bytes.Appendf(eta);
    }
    inline void inspector_gui() {
        image_value_field("Specular Reflectance", specular_reflectance);
        image_value_field("Specular Transmittance", specular_transmittance);
        image_value_field("Roughness", roughness);
        ImGui::InputFloat("eta", &eta);
    }
#endif

#ifdef __HLSL_VERSION
    float3 specular_reflectance;
    float roughness;
    float3 specular_transmittance;
    float eta;

    inline MaterialEvalRecord eval_local(const Vector3 local_dir_in, const Vector3 local_dir_out, const Real local_eta, const bool adjoint) {
        const bool reflect = local_dir_in.z * local_dir_out.z > 0;

        Vector3 local_half_vector;
        if (reflect)
            local_half_vector = normalize(local_dir_in + local_dir_out);
        else
            local_half_vector = normalize(local_dir_in + local_dir_out * local_eta);

        Real h_dot_in = dot(local_half_vector, local_dir_in);
        if (h_dot_in < 0) {
            local_half_vector = -local_half_vector;
            h_dot_in = -h_dot_in;
        }

        // Clamp roughness to avoid numerical issues.
        const Real alpha = pow2(clamp(roughness, gMinRoughness, 1));

        // Compute F / D / G
        // Note that we use the incoming direction
        // for evaluating the Fresnel reflection amount.
        // We can also use outgoing direction -- then we would need to
        // use 1/bsdf.eta and we will get the same result.
        // However, using the incoming direction allows
        // us to use F to decide whether to reflect or refract during sampling.
        const Real F = fresnel_dielectric(h_dot_in, local_eta);
        const Real D = GTR2(abs(local_half_vector.z), alpha);
        const Real G_in  = smith_masking_gtr2(local_dir_in, alpha);
        const Real G_out = smith_masking_gtr2(local_dir_out, alpha);
        MaterialEvalRecord r;
        if (reflect) {
            r.f = specular_reflectance * (F * D * (G_in * G_out)) / (4 * abs(local_dir_in.z));
            r.pdf_fwd = (F * D * G_in) / (4 * abs(local_dir_in.z));
            r.pdf_rev = (F * D * G_out) / (4 * abs(local_dir_out.z));
        } else {
            // Snell-Descartes law predicts that the light will contract/expand 
            // due to the different index of refraction. So the normal BSDF needs
            // to scale with 1/eta^2. However, the "adjoint" of the BSDF does not have
            // the eta term. This is due to the non-reciprocal nature of the index of refraction:
            // f(wi -> wo) / eta_o^2 = f(wo -> wi) / eta_i^2
            // thus f(wi -> wo) = f(wo -> wi) (eta_o / eta_i)^2
            // The adjoint of a BSDF is defined as swapping the parameter, and
            // this cancels out the eta term.
            // See Chapter 5 of Eric Veach's thesis "Robust Monte Carlo Methods for Light Transport Simulation"
            // for more details.
            const Real eta_factor = adjoint ? 1 : (1 / (local_eta * local_eta));
            const Real h_dot_out = dot(local_half_vector, local_dir_out);
            const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
            // Very complicated BSDF. See Walter et al.'s paper for more details.
            // "Microfacet Models for Refraction through Rough Surfaces"
            r.f = specular_transmittance * (eta_factor * (1 - F) * D * (G_in * G_out) * local_eta * local_eta * abs(h_dot_out * h_dot_in)) / (abs(local_dir_in.z) * sqrt_denom * sqrt_denom);
            r.pdf_fwd = ((1 - F) * D * G_in) * abs((local_eta * local_eta) * h_dot_out * h_dot_in / (local_dir_in.z * sqrt_denom * sqrt_denom));
            
            const Real rev_local_eta = 1 / local_eta;
            const Real rev_sqrt_denom = h_dot_out + rev_local_eta * h_dot_in;
            r.pdf_rev = ((1 - F) * D * G_out) * abs((rev_local_eta * rev_local_eta) * h_dot_in * h_dot_out / (local_dir_out.z * rev_sqrt_denom * rev_sqrt_denom));
        }
        return r;
    }
#endif
};

#ifdef __HLSL_VERSION
template<> inline RoughDielectric load_material(uint address, const ShadingData shading_data) {
    RoughDielectric material;
    material.specular_transmittance = sample_image(load_image_value3(address), shading_data);
    
    const ImageValue3 specular = load_image_value3(address);
    const ImageValue1 roughness = load_image_value1(address);
    const float4 s = sample_image(specular.image(), shading_data.uv, shading_data.uv_screen_size);
    material.specular_reflectance = specular.value * s.rgb;
    if (roughness.image_index() == specular.image_index)
        material.roughness = s[roughness.channel()];
    else
        material.roughness = sample_image(roughness, shading_data);

    material.eta = gMaterialData.Load<float>(address); address += 4;
    return material;
}

template<> inline bool material_has_bsdf<RoughDielectric>() { return true; }

template<> inline Spectrum eval_material_albedo(const RoughDielectric material, const ShadingData shading_data) { return material.specular_reflectance; }

template<> inline MaterialEvalRecord eval_material(const RoughDielectric material, const Vector3 dir_in, const Vector3 dir_out, const ShadingData shading_data, const bool adjoint) {
    const Vector3 local_dir_in  = shading_data.to_local(dir_in);
    const Vector3 local_dir_out = shading_data.to_local(dir_out);
    const Real local_eta = local_dir_in.z > 0 ? material.eta : 1 / material.eta;
    return material.eval_local(local_dir_in, local_dir_out, local_eta, adjoint);
}

template<> inline MaterialSampleRecord sample_material(const RoughDielectric material, const Vector3 rnd, const Vector3 dir_in, const ShadingData shading_data, const bool adjoint) {
    const Vector3 local_dir_in  = shading_data.to_local(dir_in);

    const Real rgh = clamp(material.roughness, gMinRoughness, 1);
    const Real alpha = rgh * rgh;

    // If we are going into the surface, then we use normal eta
    // (internal/external), otherwise we use external/internal.
    const Real local_eta = local_dir_in.z > 0 ? material.eta : 1 / material.eta;

    Vector3 local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);
    Real h_dot_in = dot(local_micro_normal, local_dir_in);
    if (h_dot_in < 0) {
        local_micro_normal = -local_micro_normal;
        h_dot_in = -h_dot_in;
    }

    // Now we need to decide whether to reflect or refract.
    // We do this using the Fresnel term.
    const Real F = fresnel_dielectric(h_dot_in, local_eta);

    MaterialSampleRecord r;
    r.roughness = rgh;
    if (rnd.z <= F) {
        // Reflection
        r.dir_out = normalize(-local_dir_in + 2 * h_dot_in * local_micro_normal);
        r.eta = 0;
    } else {
        // Refraction
        // https://en.wikipedia.org/wiki/Snell%27s_law#Vector_form
        // (note that our eta is eta2 / eta1, and l = -dir_in)
        const Real h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
        if (h_dot_out_sq <= 0) {
            // Total internal reflection
            // This shouldn't really happen, as F will be 1 in this case.
            r.dir_out = 0;
            r.eta = 0;
            r.f = 0;
            r.pdf_fwd = 0;
            r.pdf_rev = 0;
            return r;
        }
        const Real h_dot_out = sqrt(h_dot_out_sq);
        r.dir_out = normalize(-local_dir_in / local_eta + (abs(h_dot_in) / local_eta - h_dot_out) * local_micro_normal);
        r.eta = local_eta;
    }
    const MaterialEvalRecord eval = material.eval_local(local_dir_in, r.dir_out, local_eta, adjoint);
    r.dir_out = shading_data.to_world(r.dir_out);
    r.f = eval.f;
    r.pdf_fwd = eval.pdf_fwd;
    r.pdf_rev = eval.pdf_rev;
    return r;
}
#endif

#endif
