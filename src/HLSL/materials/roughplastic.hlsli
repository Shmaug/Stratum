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
    inline Real eval_pdfW(const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
        const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);
        if (ng_dot_in < 0 || dot(vertex.geometry_normal, dir_out) < 0) {
            // No light below the surface
            return 0;
        }
        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) < 0)
            frame.flip();

        const Vector3 half_vector = normalize(dir_in + dir_out);
        const Real n_dot_in = dot(frame.n, dir_in);
        const Real n_dot_out = dot(frame.n, dir_out);
        const Real n_dot_h = dot(frame.n, half_vector);
        if (n_dot_out <= 0 || n_dot_h <= 0) {
            return 0;
        }

        const Spectrum S = sample_image(vertex, specular_reflectance);
        const Spectrum R = sample_image(vertex, diffuse_reflectance);
        const Real lS = luminance(S), lR = luminance(R);
        if (lS + lR <= 0) {
            return 0;
        }
        // Clamp roughness to avoid numerical issues.
        const Real rgh = clamp(sample_image(vertex, roughness), gMinRoughness, 1);
        // We use the reflectance to determine whether to choose specular sampling lobe or diffuse.
        Real spec_prob = lS / (lS + lR);
        Real diff_prob = 1 - spec_prob;
        // For the specular lobe, we use the ellipsoidal sampling from Heitz 2018
        // "Sampling the GGX Distribution of Visible Normals"
        // https://jcgt.org/published/0007/04/01/
        // this importance samples smith_masking(cos_theta_in) * GTR2(cos_theta_h, roughness) * cos_theta_out
        const Real G = smith_masking_gtr2(frame.to_local(dir_in), rgh);
        const Real D = GTR2(n_dot_h, rgh);
        // (4 * cos_theta_v) is the Jacobian of the reflectiokn
        spec_prob *= (G * D) / (4 * n_dot_in);
        // For the diffuse lobe, we importance sample cos_theta_out
        diff_prob *= n_dot_out / M_PI;
        return spec_prob + diff_prob;
    }
    inline void load(ByteAddressBuffer bytes, inout uint address) {
        diffuse_reflectance.load(bytes, address);
        specular_reflectance.load(bytes, address);
        roughness.load(bytes, address);
        eta = bytes.Load<float>(address); address += 4;
    }
#endif // __HLSL_VERSION
};

#ifdef __HLSL_VERSION
template<> inline BSDFEvalRecord eval_material(const RoughPlastic material, Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir) {
    const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);
    if (ng_dot_in < 0 || dot(vertex.geometry_normal, dir_out) < 0) {
        // No light below the surface
        BSDFEvalRecord r;
        r.pdfW = 0;
        r.f = 0;
        return r;
    }
    // Flip the shading frame if it is inconsistent with the geometry normal
    ShadingFrame frame = vertex.shading_frame();
    if (dot(frame.n, dir_in) < 0)
        frame.flip();

    // The half-vector is a crucial component of the microfacet models.
    // Since microfacet assumes that the surface is made of many small mirrors/glasses,
    // The "average" between input and output direction determines the orientation
    // of the mirror our ray hits (since applying reflection of dir_in over half_vector
    // gives us dir_out). Microfacet models build all sorts of quantities based on the
    // half vector. It's also called the "micro normal".
    const Vector3 half_vector = normalize(dir_in + dir_out);
    const Real n_dot_h = dot(frame.n, half_vector);
    const Real n_dot_in = dot(frame.n, dir_in);
    const Real n_dot_out = dot(frame.n, dir_out);
    if (n_dot_out <= 0 || n_dot_h <= 0) {
        BSDFEvalRecord r;
        r.pdfW = 0;
        r.f = 0;
        return r;
    }

    // Clamp roughness to avoid numerical issues.
    const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);

    // If we are going into the surface, then we use normal eta
    // (internal/external), otherwise we use external/internal.
    const Real local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? material.eta : 1 / material.eta;

    // We first account for the dielectric layer.

    // Fresnel equation determines how much light goes through, 
    // and how much light is reflected for each wavelength.
    // Fresnel equation is determined by the angle between the (micro) normal and 
    // both incoming and outgoing directions (dir_out & dir_in).
    // However, since they are related through the Snell-Descartes law,
    // we only need one of them.
    const Real F_o = fresnel_dielectric(dot(half_vector, dir_out), local_eta); // F_o is the reflection percentage.
    const Real D = GTR2(n_dot_h, rgh); // "Generalized Trowbridge Reitz", GTR2 is equivalent to GGX.
    const Real G = smith_masking_gtr2(frame.to_local(dir_in), rgh) * smith_masking_gtr2(frame.to_local(dir_out), rgh);

    const Spectrum Ks = sample_image(vertex, material.specular_reflectance);

    const Spectrum spec_contrib = Ks * (G * F_o * D) / (4 * n_dot_in * n_dot_out);

    // Next we account for the diffuse layer.
    // In order to reflect from the diffuse layer,
    // the photon needs to bounce through the dielectric layers twice.
    // The transmittance is computed by 1 - fresnel.
    const Real F_i = fresnel_dielectric(dot(half_vector, dir_in), local_eta);
    // Multiplying with Fresnels leads to an overly dark appearance at the 
    // object boundaries. Disney BRDF proposes a fix to this -- we will implement this in problem set 1.
    const Spectrum Kd = sample_image(vertex, material.diffuse_reflectance);
    const Spectrum diffuse_contrib = Kd * (1 - F_o) * (1 - F_i) / M_PI;

    BSDFEvalRecord r;
    r.pdfW = material.eval_pdfW(dir_in, dir_out, vertex);
    r.f = (spec_contrib + diffuse_contrib) * n_dot_out;
    return r;
}

template<> inline BSDFSampleRecord sample_material(const RoughPlastic material, Vector3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir) {
    const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);
    if (ng_dot_in < 0) {
        // No light below the surface
        BSDFSampleRecord r;
        r.dir_out = 0;
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }
    // Flip the shading frame if it is inconsistent with the geometry normal
    ShadingFrame frame = vertex.shading_frame();
    if (dot(frame.n, dir_in) < 0)
        frame.flip();

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
    const Real spec_prob = lS / (lS + lR);
    if (rnd.z < spec_prob) {
        // Sample from the specular lobe.

        // Convert the incoming direction to local coordinates
        const Vector3 local_dir_in = frame.to_local(dir_in);
        // Clamp roughness to avoid numerical issues.
        const Real rgh = clamp(sample_image(vertex, material.roughness), gMinRoughness, 1);
        const Real alpha = rgh * rgh;
        Vector3 local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);
        
        // Transform the micro normal to world space
        Vector3 half_vector = frame.to_world(local_micro_normal);
        BSDFSampleRecord r;
        // Reflect over the world space normal
        r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
        r.eta = 0;
        r.roughness = (min16float)rgh;
        r.eval = eval_material(material, dir_in, r.dir_out, vertex, dir);
        return r;
    } else {
        BSDFSampleRecord r;
        r.dir_out = frame.to_world(sample_cos_hemisphere(rnd.x, rnd.y));
        r.eta = 0;
        r.roughness = 1;
        r.eval = eval_material(material, dir_in, r.dir_out, vertex, dir);
        return r;
    }
}

template<> inline Spectrum eval_material_albedo(const RoughPlastic material, const PathVertexGeometry vertex) { return sample_image(vertex, material.diffuse_reflectance); }
#endif

#endif