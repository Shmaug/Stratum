#ifndef ROUGHDIELECTRIC_H
#define ROUGHDIELECTRIC_H

#include "../scene.hlsli"
#ifdef __HLSL_VERSION
#include "../microfacet.hlsli"
#endif

struct RoughDielectric {
    ImageValue3 specular_reflectance;
    ImageValue3 specular_transmittance;
    ImageValue1 roughness;
    float eta;

#ifdef __cplusplus
    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        specular_reflectance.store(bytes, resources);
        specular_transmittance.store(bytes, resources);
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
    inline void load(ByteAddressBuffer bytes, inout uint address) {
        specular_reflectance.load(bytes, address);
        specular_transmittance.load(bytes, address);
        roughness.load(bytes, address);
        eta = bytes.Load<float>(address); address += 4;
    }
#endif // __HLSL_VERSION
};

#ifdef __HLSL_VERSION
template<> inline BSDFEvalRecord eval_material(const RoughDielectric material, const Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
    ShadingFrame frame = gPathVertices[vertex].shading_frame();
    if (dot(frame.n, dir_in) < 0)
        frame.flip();

    const bool reflect = dot(frame.n, dir_in) * dot(frame.n, dir_out) > 0;

    // If we are going into the surface, then we use normal eta
    // (internal/external), otherwise we use external/internal.
    const Real local_eta = dot(gPathVertices[vertex].geometry_normal, dir_in) > 0 ? material.eta : 1 / material.eta;

    const Spectrum Ks = sample_image(vertex, material.specular_reflectance);
    const Spectrum Kt = sample_image(vertex, material.specular_transmittance);
    // Clamp roughness to avoid numerical issues.
    const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);

    Vector3 half_vector;
    if (reflect) {
        half_vector = normalize(dir_in + dir_out);
    } else {
        // "Generalized half-vector" from Walter et al.
        // See "Microfacet Models for Refraction through Rough Surfaces"
        half_vector = normalize(dir_in + dir_out * local_eta);
    }

    // Flip half-vector if it's below surface
    Real h_dot_n = dot(half_vector, frame.n);
    if (h_dot_n < 0) {
        half_vector = -half_vector;
        h_dot_n = -h_dot_n;
    }

    // Compute F / D / G
    // Note that we use the incoming direction
    // for evaluating the Fresnel reflection amount.
    // We can also use outgoing direction -- then we would need to
    // use 1/bsdf.eta and we will get the same result.
    // However, using the incoming direction allows
    // us to use F to decide whether to reflect or refract during sampling.
    const Real h_dot_in = dot(half_vector, dir_in);
    const Real F = fresnel_dielectric(h_dot_in, local_eta);
    const Real D = GTR2(h_dot_n, rgh);
    const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
    const Real G_out = smith_masking_gtr2(frame.to_local(dir_out), rgh);
    BSDFEvalRecord r;
    if (reflect) {
        r.f = Ks * (F * D * (G_in * G_out)) / (4 * abs(dot(frame.n, dir_in)));
        r.pdfW = (F * D * G_in) / (4 * abs(dot(frame.n, dir_in)));
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
        const Real eta_factor = dir == TRANSPORT_TO_LIGHT ? (1 / (local_eta * local_eta)) : 1;
        const Real h_dot_out = dot(half_vector, dir_out);
        const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
        // Very complicated BSDF. See Walter et al.'s paper for more details.
        // "Microfacet Models for Refraction through Rough Surfaces"
        r.f = Kt * (eta_factor * (1 - F) * D * (G_in * G_out) * local_eta * local_eta * abs(h_dot_out * h_dot_in)) / (abs(dot(frame.n, dir_in)) * sqrt_denom * sqrt_denom);
        const Real dh_dout = (local_eta * local_eta) * h_dot_out / (sqrt_denom * sqrt_denom);
        r.pdfW = ((1 - F) * D * G_in) * abs(dh_dout * h_dot_in / dot(frame.n, dir_in));
    }
    return r;
}

template<> inline BSDFSampleRecord sample_material(const RoughDielectric material, const Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
    // Flip the shading frame if it is inconsistent with the geometry normal
    ShadingFrame frame = gPathVertices[vertex].shading_frame();
    if (dot(frame.n, dir_in) < 0)
        frame.flip();

    // Clamp roughness to avoid numerical issues.
    const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);
    // Sample a micro normal and transform it to world space -- this is our half-vector.
    const Real alpha = rgh * rgh;
    const Vector3 local_dir_in = frame.to_local(dir_in);
    const Vector3 local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);

    Vector3 half_vector = frame.to_world(local_micro_normal);
    Real h_dot_n = dot(half_vector, frame.n);
    if (h_dot_n < 0) {
        half_vector = -half_vector;
        h_dot_n = -h_dot_n;
    }

    // If we are going into the surface, then we use normal eta
    // (internal/external), otherwise we use external/internal.
    const Real local_eta = dot(gPathVertices[vertex].geometry_normal, dir_in) > 0 ? material.eta : 1 / material.eta;

    const Real h_dot_in = dot(half_vector, dir_in);
    const Real F = fresnel_dielectric(h_dot_in, local_eta);
    const Real D = GTR2(h_dot_n, rgh);
    
    if (rnd.z <= F) {
        const Spectrum Ks = sample_image(vertex, material.specular_reflectance);

        // Reflection
        BSDFSampleRecord r;
        r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
        r.eta = 0;
        r.roughness = rgh;
        const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
        const Real G_out = smith_masking_gtr2(frame.to_local(r.dir_out), rgh);
        r.eval.f = Ks * (F * D * (G_in * G_out)) / (4 * abs(dot(frame.n, dir_in)));
        r.eval.pdfW = (F * D * G_in) / (4 * abs(dot(frame.n, dir_in)));
        return r;
    } else {
        // Refraction
        // https://en.wikipedia.org/wiki/Snell%27s_law#Vector_form
        // (note that our eta is eta2 / eta1, and l = -dir_in)
        const Real h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
        if (h_dot_out_sq <= 0) {
            // Total internal reflection
            // This shouldn't really happen, as F will be 1 in this case.
            BSDFSampleRecord r;
            r.dir_out = 0;
            r.eta = 0;
            r.eval.f = 0;
            r.eval.pdfW = 0;
            return r;
        }
        // flip half_vector if needed
        if (h_dot_in < 0)
            half_vector = -half_vector;

        const Spectrum Kt = sample_image(vertex, material.specular_transmittance);
        
        const Real h_dot_out = sqrt(h_dot_out_sq);
        BSDFSampleRecord r;
        r.dir_out = normalize(-dir_in / local_eta + (abs(h_dot_in) / local_eta - h_dot_out) * half_vector);
        r.eta = (min16float)local_eta;
        r.roughness = (min16float)rgh;
        const Real eta_factor = dir == TRANSPORT_TO_LIGHT ? (1 / (local_eta * local_eta)) : 1;
        const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
        const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
        const Real G_out = smith_masking_gtr2(frame.to_local(r.dir_out), rgh);
        r.eval.f = Kt * (eta_factor * (1 - F) * D * (G_in * G_out) * local_eta * local_eta * abs(h_dot_out * h_dot_in)) / (abs(dot(frame.n, dir_in)) * sqrt_denom * sqrt_denom);
        const Real dh_dout = (local_eta * local_eta) * h_dot_out / (sqrt_denom * sqrt_denom);
        r.eval.pdfW = ((1 - F) * D * G_in) * abs(dh_dout * h_dot_in / dot(frame.n, dir_in));
        return r;
    }
}

template<> inline Spectrum eval_material_albedo(const RoughDielectric material, const uint vertex) { return sample_image(vertex, material.specular_reflectance); }
#endif

#endif
