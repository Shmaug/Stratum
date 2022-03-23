#ifndef ROUGHPLASTIC_H
#define ROUGHPLASTIC_H

#include "../scene.hlsli"
#ifdef __HLSL_VERSION
#include "../microfacet.hlsli"
#endif

struct RoughPlastic {
    ImageValue3 diffuse_reflectance;
    ImageValue3 specular_reflectance;
    ImageValue1 roughness;
    float eta;

#ifdef __cplusplus
    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        diffuse_reflectance.store(bytes, resources);
        specular_reflectance.store(bytes, resources);
        roughness.store(bytes, resources);
        bytes.Appendf(eta);
    }
    inline void inspector_gui() {
        image_value_field("Diffuse Reflectance", diffuse_reflectance);
        image_value_field("Specular Reflectance", specular_reflectance);
        image_value_field("Roughness", roughness);
        ImGui::InputFloat("eta", &eta);
    }
#endif
#ifdef __HLSL_VERSION
    inline void load(ByteAddressBuffer bytes, inout uint address) {
        diffuse_reflectance.load(bytes, address);
        specular_reflectance.load(bytes, address);
        roughness.load(bytes, address);
        eta = bytes.Load<float>(address); address += 4;
    }
#endif // __HLSL_VERSION
};

#ifdef __HLSL_VERSION
template<> inline BSDFEvalRecord eval_material(const RoughPlastic material, Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
    const ShadingFrame frame = gPathVertices[vertex].shading_frame();
    const Real n_dot_in = dot(frame.n, dir_in);
    const Real n_dot_out = dot(frame.n, dir_out);
    if (n_dot_out <= 0 || n_dot_in <= 0) {
        // No light below the surface
        BSDFEvalRecord r;
        r.f = 0;
        r.pdfW = 0;
        return r;
    }

    // The half-vector is a crucial component of the microfacet models.
    // Since microfacet assumes that the surface is made of many small mirrors/glasses,
    // The "average" between input and output direction determines the orientation
    // of the mirror our ray hits (since applying reflection of dir_in over half_vector
    // gives us dir_out). Microfacet models build all sorts of quantities based on the
    // half vector. It's also called the "micro normal".
    const Vector3 half_vector = normalize(dir_in + dir_out);
    const Real n_dot_h = dot(frame.n, half_vector);

    // Clamp roughness to avoid numerical issues.
    const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);

    // If we are going into the surface, then we use normal eta
    // (internal/external), otherwise we use external/internal.
    const Real local_eta = dot(gPathVertices[vertex].geometry_normal, dir_in) > 0 ? material.eta : 1 / material.eta;

    // We first account for the dielectric layer.

    // Fresnel equation determines how much light goes through, 
    // and how much light is reflected for each wavelength.
    // Fresnel equation is determined by the angle between the (micro) normal and 
    // both incoming and outgoing directions (dir_out & dir_in).
    // However, since they are related through the Snell-Descartes law,
    // we only need one of them.
    const Real F_i = fresnel_dielectric(dot(half_vector, dir_in), local_eta);
    const Real F_o = fresnel_dielectric(dot(half_vector, dir_out), local_eta); // F_o is the reflection percentage.
    const Real D = GTR2(n_dot_h, rgh); // "Generalized Trowbridge Reitz", GTR2 is equivalent to GGX.
    const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
    const Real G_out = smith_masking_gtr2(frame.to_local(dir_out), rgh);

    const Spectrum Ks = sample_image(vertex, material.specular_reflectance);
    const Spectrum Kd = sample_image(vertex, material.diffuse_reflectance);

    const Spectrum spec_contrib = Ks * ((G_in * G_out) * F_o * D) / (4 * n_dot_in * n_dot_out);

    // Next we account for the diffuse layer.
    // In order to reflect from the diffuse layer,
    // the photon needs to bounce through the dielectric layers twice.
    // The transmittance is computed by 1 - fresnel.
    const Spectrum diffuse_contrib = Kd * (1 - F_o) * (1 - F_i) / M_PI;

    BSDFEvalRecord r;
    r.f = (spec_contrib + diffuse_contrib) * n_dot_out;

    const Real lS = luminance(Ks);
    const Real lR = luminance(Kd);
    if (lS + lR <= 0) {
        r.pdfW = 0;
    } else {
        // We use the reflectance to determine whether to choose specular sampling lobe or diffuse.
        const Real spec_prob = lS / (lS + lR);
        const Real diff_prob = 1 - spec_prob;
        // For the specular lobe, we use the ellipsoidal sampling from Heitz 2018
        // "Sampling the GGX Distribution of Visible Normals"
        // https://jcgt.org/published/0007/04/01/
        // this importance samples smith_masking(cos_theta_in) * GTR2(cos_theta_h, roughness) * cos_theta_out
        // (4 * cos_theta_v) is the Jacobian of the reflectiokn
        // For the diffuse lobe, we importance sample cos_theta_out
        r.pdfW = spec_prob * (G_in * D) / (4 * n_dot_in) + diff_prob * (n_dot_out / M_PI);
    }
    return r;
}

template<> inline BSDFSampleRecord sample_material(const RoughPlastic material, Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
    const ShadingFrame frame = gPathVertices[vertex].shading_frame();
    const Real n_dot_in = dot(frame.n, dir_in);
    if (n_dot_in < 0) {
        // No light below the surface
        BSDFSampleRecord r;
        r.dir_out = 0;
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    // We use the reflectance to choose between sampling the dielectric or diffuse layer.
    const Spectrum Ks = sample_image(vertex, material.specular_reflectance);
    const Spectrum Kd = sample_image(vertex, material.diffuse_reflectance);
    const Real lS = luminance(Ks), lR = luminance(Kd);
    if (lS + lR <= 0) {
        BSDFSampleRecord r;
        r.dir_out = 0;
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    // Clamp roughness to avoid numerical issues.
    const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);

    const Real spec_prob = lS / (lS + lR);
    BSDFSampleRecord r;
    Vector3 half_vector;
    if (rnd.z < spec_prob) {
        // Sample from the specular lobe.

        // Convert the incoming direction to local coordinates
        const Vector3 local_dir_in = frame.to_local(dir_in);
        const Real alpha = rgh * rgh;
        const Vector3 local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);
        
        // Transform the micro normal to world space
        half_vector = frame.to_world(local_micro_normal);
        // Reflect over the world space normal
        r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
        r.eta = 0;
        r.roughness = rgh;
    } else {
        r.dir_out = frame.to_world(sample_cos_hemisphere(rnd.x, rnd.y));
        r.eta = 0;
        r.roughness = 1;
        half_vector = normalize(dir_in + r.dir_out);
    }
    
    const Real n_dot_out = dot(frame.n, r.dir_out);
    const Real F_i = fresnel_dielectric(dot(half_vector, dir_in), material.eta);
    const Real F_o = fresnel_dielectric(dot(half_vector, r.dir_out), material.eta);
    const Real D = GTR2(dot(half_vector, frame.n), rgh);
    const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
    const Real G_out = smith_masking_gtr2(frame.to_local(r.dir_out), rgh);
    const Spectrum spec_contrib = Ks * ((G_in * G_out) * F_o * D) / (4 * n_dot_in * n_dot_out);
    const Spectrum diffuse_contrib = Kd * (1 - F_o) * (1 - F_i) / M_PI;
    r.eval.f = (spec_contrib + diffuse_contrib) * n_dot_out;
    r.eval.pdfW = spec_prob*(G_in * D) / (4 * n_dot_in) + (1 - spec_prob)*n_dot_out / M_PI;
    return r;
}

template<> inline Spectrum eval_material_albedo(const RoughPlastic material, const uint vertex) { return sample_image(vertex, material.diffuse_reflectance); }
#endif

#endif