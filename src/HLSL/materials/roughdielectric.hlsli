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

#ifdef __HLSL_VERSION

    template<typename Real>
    inline Real eval_pdfW(const vector<Real,3> dir_in, const vector<Real,3> dir_out, const PathVertexGeometry vertex) {
        const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);

        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * ng_dot_in < 0)
            frame.flip();
            
        const bool reflect = ng_dot_in * dot(vertex.geometry_normal, dir_out) > 0;
        
        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const Real local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? eta : 1 / eta;

        vector<Real,3> half_vector;
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
        const Real rgh = clamp(vertex.eval(roughness), gMinRoughness, 1);

        // We sample the visible normals, also we use F to determine
        // whether to sample reflection or refraction
        // so PDF ~ F * D * G_in for reflection, PDF ~ (1 - F) * D * G_in for refraction.
        const Real h_dot_in = dot(half_vector, dir_in);
        const Real F = fresnel_dielectric(h_dot_in, local_eta);
        const Real D = GTR2(dot(half_vector, frame.n), rgh);
        const Real G_in = smith_masking_gtr2(frame.to_local(dir_in), rgh);
        if (reflect) {
            return (F * D * G_in) / (4 * abs(dot(frame.n, dir_in)));
        } else {
            const Real h_dot_out = dot(half_vector, dir_out);
            const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
            const Real dh_dout = (local_eta * local_eta) * h_dot_out / (sqrt_denom * sqrt_denom);
            return ((1 - F) * D * G_in) * abs(dh_dout * h_dot_in / dot(frame.n, dir_in));
        }
    }
    
    template<typename Real, bool TransportToLight>
    inline BSDFEvalRecord<Real> eval(const vector<Real,3> dir_in, const vector<Real,3> dir_out, const PathVertexGeometry vertex) {
        const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);

        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * ng_dot_in < 0)
            frame.flip();

        const bool reflect = ng_dot_in * dot(vertex.geometry_normal, dir_out) > 0;

        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const Real local_eta = ng_dot_in > 0 ? eta : 1 / eta;

        const vector<Real,3> Ks = vertex.eval(specular_reflectance);
        const vector<Real,3> Kt = vertex.eval(specular_transmittance);
        // Clamp roughness to avoid numerical issues.
        const Real rgh = clamp(vertex.eval(roughness), gMinRoughness, 1);

        vector<Real,3> half_vector;
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
        const Real G = smith_masking_gtr2(frame.to_local(dir_in), rgh) * smith_masking_gtr2(frame.to_local(dir_out), rgh);
        BSDFEvalRecord<Real> r;
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
            const Real eta_factor = TransportToLight ? (1 / (local_eta * local_eta)) : 1;
            const Real h_dot_out = dot(half_vector, dir_out);
            const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
            // Very complicated BSDF. See Walter et al.'s paper for more details.
            // "Microfacet Models for Refraction through Rough Surfaces"
            r.f = Kt * (eta_factor * (1 - F) * D * G * local_eta * local_eta * abs(h_dot_out * h_dot_in)) / (abs(dot(frame.n, dir_in)) * sqrt_denom * sqrt_denom);
        }
        r.pdfW = eval_pdfW(dir_in, dir_out, vertex);
        return r;
    }

    template<typename Real, bool TransportToLight>
    inline BSDFSampleRecord<Real> sample(const vector<Real,3> rnd, const vector<Real,3> dir_in, const PathVertexGeometry vertex) {
        const Real ng_dot_in = dot(vertex.geometry_normal, dir_in);

        // If we are going into the surface, then we use normal eta
        // (internal/external), otherwise we use external/internal.
        const Real local_eta = dot(vertex.geometry_normal, dir_in) > 0 ? eta : 1 / eta;

        // Flip the shading frame if it is inconsistent with the geometry normal
        ShadingFrame frame = vertex.shading_frame();
        if (dot(frame.n, dir_in) * ng_dot_in < 0)
            frame.flip();

        // Clamp roughness to avoid numerical issues.
        const Real rgh = clamp(vertex.eval(roughness), gMinRoughness, 1);
        // Sample a micro normal and transform it to world space -- this is our half-vector.
        const Real alpha = rgh * rgh;
        const vector<Real,3> local_dir_in = frame.to_local(dir_in);
        const vector<Real,3> local_micro_normal = sample_visible_normals(local_dir_in, alpha, rnd.xy);

        vector<Real,3> half_vector = frame.to_world(local_micro_normal);
        // Flip half-vector if it's below surface
        if (dot(half_vector, frame.n) < 0)
            half_vector = -half_vector;

        // Now we need to decide whether to reflect or refract.
        // We do this using the Fresnel term.
        const Real h_dot_in = dot(half_vector, dir_in);
        const Real F = fresnel_dielectric(h_dot_in, local_eta);

        if (rnd.z <= F) {
            // Reflection
            BSDFSampleRecord<Real> r;
            r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
            r.eta = 0;
            r.roughness = rgh;
            r.eval = eval<Real, TransportToLight>(dir_in, r.dir_out, vertex);
            return r;
        } else {
            // Refraction
            // https://en.wikipedia.org/wiki/Snell%27s_law#Vector_form
            // (note that our eta is eta2 / eta1, and l = -dir_in)
            const Real h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
            if (h_dot_out_sq <= 0) {
                // Total internal reflection
                // This shouldn't really happen, as F will be 1 in this case.
                BSDFSampleRecord<Real> r;
                r.dir_out = 0;
                r.eta = 0;
                r.eval.f = 0;
                r.eval.pdfW = 0;
                return r;
            }
            // flip half_vector if needed
            if (h_dot_in < 0)
                half_vector = -half_vector;
            
            const Real h_dot_out = sqrt(h_dot_out_sq);
            BSDFSampleRecord<Real> r;
            r.dir_out = normalize(-dir_in / local_eta + (abs(h_dot_in) / local_eta - h_dot_out) * half_vector);
            r.eta = local_eta;
            r.roughness = rgh;
            r.eval = eval<Real, TransportToLight>(dir_in, r.dir_out, vertex);
            return r;
        }
    }

    template<typename Real> inline vector<Real,3> eval_albedo  (const PathVertexGeometry vertex) { return vertex.eval(specular_reflectance); }
    template<typename Real> inline vector<Real,3> eval_emission(const PathVertexGeometry vertex) { return 0; }

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
