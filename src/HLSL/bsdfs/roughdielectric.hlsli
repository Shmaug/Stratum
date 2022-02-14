#ifndef ROUGHDIELECTRIC_H
#define ROUGHDIELECTRIC_H

#include "../scene.hlsli"
#include "../microfacet.hlsli"

struct RoughDielectric : BSDF {
    ImageValue3 specular_reflectance;
    ImageValue3 specular_transmittance;
    ImageValue1 roughness;
    float eta;

#ifdef __HLSL_VERSION

    inline float eval_pdfW(const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * dot(vertex.geometry_normal, dir_in) < 0)
            frame.flip();
            
        const bool reflect = dot(frame.n, dir_in) * dot(frame.n, dir_out) > 0;
        
        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const float local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? eta : 1 / eta;

        float3 half_vector;
        if (reflect) {
            half_vector = normalize(dir_in + dir_out);
        } else {
            // "Generalized half-vector" from Walter et al.
            // See "Microfacet Models for Refraction through Rough Surfaces"
            half_vector = normalize(dir_in + dir_out * local_eta);
        }

        // Flip half-vector if it's below surface
        if (dot(half_vector, frame.n) < 0) {
            half_vector = -half_vector;
        }

        // Clamp roughness to avoid numerical issues.
        const float rgh = clamp(roughness.eval(vertex), 0.01, 1);

        // We sample the visible normals, also we use F to determine
        // whether to sample reflection or refraction
        // so PDF ~ F * D * G_in for reflection, PDF ~ (1 - F) * D * G_in for refraction.
        const float h_dot_in = dot(half_vector, dir_in);
        const float F = fresnel_dielectric(h_dot_in, local_eta);
        const float D = GTR2(dot(half_vector, frame.n), rgh);
        const float G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
        if (reflect) {
            return (F * D * G_in) / (4 * abs(dot(frame.n, dir_in)));
        } else {
            const float h_dot_out = dot(half_vector, dir_out);
            const float sqrt_denom = h_dot_in + local_eta * h_dot_out;
            const float dh_dout = local_eta * local_eta * h_dot_out / (sqrt_denom * sqrt_denom);
            return (1 - F) * D * G_in * abs(dh_dout * h_dot_in / dot(frame.n, dir_in));
        }
    }
    inline BSDFEvalRecord eval(const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * dot(vertex.geometry_normal, dir_in) < 0)
            frame.flip();

        const bool reflect = dot(frame.n, dir_in) * dot(frame.n, dir_out) > 0;

        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const float local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? eta : 1 / eta;

        const float3 Ks = specular_reflectance.eval(vertex);
        const float3 Kt = specular_transmittance.eval(vertex);
        // Clamp roughness to avoid numerical issues.
        const float rgh = clamp(roughness.eval(vertex), 0.025, 1);

        float3 half_vector;
        if (reflect) {
            half_vector = normalize(dir_in + dir_out);
        } else {
            // "Generalized half-vector" from Walter et al.
            // See "Microfacet Models for Refraction through Rough Surfaces"
            half_vector = normalize(dir_in + dir_out * local_eta);
        }

        // Flip half-vector if it's below surface
        float h_dot_n = dot(half_vector, frame.n);
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
        const float h_dot_in = dot(half_vector, dir_in);
        const float F = fresnel_dielectric(h_dot_in, local_eta);
        const float D = GTR2(h_dot_n, rgh);
        const float G = smith_masking_gtr2(frame.to_local(dir_in), rgh) * smith_masking_gtr2(frame.to_local(dir_out), rgh);
        BSDFEvalRecord r;
        if (reflect) {
            r.f = Ks * (F * D * G) / (4 * abs(dot(frame.n, dir_in)));
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
            const float eta_factor = dir == TransportDirection::eToLight ? (1 / (local_eta * local_eta)) : 1;
            const float h_dot_out = dot(half_vector, dir_out);
            const float sqrt_denom = h_dot_in + local_eta * h_dot_out;
            // Very complicated BSDF. See Walter et al.'s paper for more details.
            // "Microfacet Models for Refraction through Rough Surfaces"
            r.f = Kt * (eta_factor * (1 - F) * D * G * local_eta * local_eta * abs(h_dot_out * h_dot_in)) / (abs(dot(frame.n, dir_in)) * sqrt_denom * sqrt_denom);
        }
        r.pdfW = eval_pdfW(dir_in, dir_out, vertex, dir);
        return r;
    }

    inline BSDFSampleRecord sample(const float3 rnd, const float3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const float local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? eta : 1 / eta;

        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * dot(vertex.geometry_normal, dir_in) < 0)
            frame.flip();

        // Clamp roughness to avoid numerical issues.
        const float rgh = clamp(roughness.eval(vertex), 0.01, 1);
        // Sample a micro normal and transform it to world space -- this is our half-vector.
        const float alpha = rgh * rgh;
        const float3 local_dir_in = frame.to_local(dir_in);
        const float3 local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);

        float3 half_vector = frame.to_world(local_micro_normal);
        // Flip half-vector if it's below surface
        if (dot(half_vector, frame.n) < 0)
            half_vector = -half_vector;

        // Now we need to decide whether to reflect or refract.
        // We do this using the Fresnel term.
        const float h_dot_in = dot(half_vector, dir_in);
        const float F = fresnel_dielectric(h_dot_in, local_eta);

        if (rnd.z <= F) {
            // Reflection
            BSDFSampleRecord r;
            r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
            r.eta = 0;
            r.eval = eval(dir_in, r.dir_out, vertex, dir);
            return r;
        } else {
            // Refraction
            // https://en.wikipedia.org/wiki/Snell%27s_law#Vector_form
            // (note that our eta is eta2 / eta1, and l = -dir_in)
            const float h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
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
            
            const float h_dot_out = sqrt(h_dot_out_sq);
            BSDFSampleRecord r;
            r.dir_out = -dir_in / local_eta + (abs(h_dot_in) / local_eta - h_dot_out) * half_vector;
            r.eta = 0;
            r.eval = eval(dir_in, r.dir_out, vertex, dir);
            return r;
        }
    }

    inline float3 eval_albedo(const PathVertexGeometry vertex) { return specular_reflectance.eval(vertex); }
    inline float3 eval_emission(const PathVertexGeometry vertex) { return 0; }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        specular_reflectance.load(bytes, address);
        specular_transmittance.load(bytes, address);
        roughness.load(bytes, address);
        eta = asfloat(bytes.Load(address)); address += 4;
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus

    inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
        specular_reflectance.store(bytes, images);
        specular_transmittance.store(bytes, images);
        roughness.store(bytes, images);
        bytes.Append(asuint(eta));
    }
    inline void inspector_gui() {
        image_value_field("Specular Reflectance", specular_reflectance);
        image_value_field("Specular Transmittance", specular_transmittance);
        image_value_field("Roughness", roughness);
        ImGui::InputFloat("eta", &eta);
    }

#endif
};

#endif
