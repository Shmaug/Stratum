Real Dm(const Real alpha_x, const Real alpha_y, const Vector3 h_l) {
    const Real alpha_x2 = alpha_x * alpha_x;
    const Real alpha_y2 = alpha_y * alpha_y;
    const Vector3 h_l2 = h_l * h_l;
    const Real hh = h_l2.x/alpha_x2 + h_l2.y/alpha_y2 + h_l2.z;
    return 1 / (M_PI * alpha_x * alpha_y * hh*hh);
}
Real G1(const Real alpha_x, const Real alpha_y, const Vector3 w_l) {
    const Real alpha_x2 = alpha_x * alpha_x;
    const Real alpha_y2 = alpha_y * alpha_y;
    const Vector3 w_l2 = w_l * w_l;
    const Real lambda = (sqrt(1 + (w_l2.x*alpha_x2 + w_l2.y*alpha_y2) / w_l2.z) - 1) / 2;
    return 1 / (1 + lambda);
}

struct DisneyGlass : BSDF {
	Spectrum base_color;
	Real alpha_x, alpha_y;
	Real eta;

	Real eval_pdf(const Vector3 dir_in, const Vector3 dir_out, const Vector3 h, const bool reflect) {
		// We sample the visible normals, also we use F to determine
		// whether to sample reflection or refraction
		// so PDF ~ F * D * G_in for reflection, PDF ~ (1 - F) * D * G_in for refraction.
		const Real h_dot_in = dot(half_vector, dir_in);
		const Real F = fresnel_dielectric(h_dot_in, eta);

		const Real D = Dm(alpha_x, alpha_y, half_vector);
		const Real G_in = G1(alpha_x, alpha_y, dir_in);

		if (reflect) {
			return (F * D * G_in) / (4 * abs(dir_in.z));
		} else {
			const Real h_dot_out = dot(half_vector, dir_out);
			const Real sqrt_denom = h_dot_in + eta * h_dot_out;
			const Real dh_dout = eta * eta * h_dot_out / (sqrt_denom * sqrt_denom);
			return (1 - F) * D * G_in * abs(dh_dout * h_dot_in / dir_in.z);
		}
	}

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		const bool reflect = dir_in.z * dir_out.z > 0;

		// If we are going into the surface, then we use normal eta
		// (internal/external), otherwise we use external/internal.
		const Real local_eta = dir_in.z > 0 ? eta : 1 / eta;

		Vector3 half_vector;
		if (reflect) {
			half_vector = normalize(dir_in + dir_out);
		} else {
			// "Generalized half-vector" from Walter et al.
			// See "Microfacet Models for Refraction through Rough Surfaces"
			half_vector = normalize(dir_in + dir_out * local_eta);
		}

		// Flip half-vector if it's below surface
		if (half_vector.z * dir_in.z < 0) half_vector = -half_vector;

		// Compute F / D / G
		// Note that we use the incoming direction
		// for evaluating the Fresnel reflection amount.
		// We can also use outgoing direction -- then we would need to
		// use 1/bsdf.eta and we will get the same result.
		// However, using the incoming direction allows
		// us to use F to decide whether to reflect or refract during sampling.
		const Real h_dot_in = dot(half_vector, dir_in);
		const Real F = fresnel_dielectric(h_dot_in, local_eta);
		const Real D = Dm(alpha_x, alpha_y, half_vector));
		const Real G = G1(alpha_x, alpha_y, dir_in) * G1(alpha_x, alpha_y, dir_out);
		if (reflect) {
			r.f = base_color * (F * D * G) / (4 * abs(dir_in.z));
			r.pdf_fwd = ;
			r.pdf_rev = ;
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
			const Real eta_factor = adjoint ? (1 / (local_eta * local_eta)) : 1;
			const Real h_dot_out = dot(half_vector, dir_out);
			const Real sqrt_denom = h_dot_in + local_eta * h_dot_out;
			// Very complicated BSDF. See Walter et al.'s paper for more details.
			// "Microfacet Models for Refraction through Rough Surfaces"
			r.f = sqrt(base_color) * ((1 - F) * D * G * abs(h_dot_out * h_dot_in)) / (abs(dir_in.z) * sqrt_denom * sqrt_denom);
			r.pdf_fwd = ;
			r.pdf_rev = ;
		}
	}

	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		// If we are going into the surface, then we use normal eta
		// (internal/external), otherwise we use external/internal.
		const Real local_eta = dir_in.z > 0 ? eta : 1 / eta;

		// Sample a micro normal and transform it to world space -- this is our half-vector.
		const Vector3 half_vector = sample_visible_normals(dir_in, alpha_x, alpha_y, rnd.xy);

		// Now we need to decide whether to reflect or refract.
		// We do this using the Fresnel term.
		const Real h_dot_in = dot(half_vector, dir_in);
		const Real F = fresnel_dielectric(h_dot_in, local_eta);

		Real h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
		if (h_dot_out_sq <= 0 || rnd_param_w <= F) {
			// Reflection
			// set eta to 0 since we are not transmitting
			r.dir_out = reflect(-dir_in, half_vector);
			r.pdf_fwd = ;
			r.pdf_rev = ;
			r.eta = 0;
			r.roughness = sqrt(alpha_x*alpha_y);
		} else {
			// Refraction

			Real h_dot_out = sqrt(h_dot_out_sq);
			r.dir_out = refract(-dir_in, half_vector);
			r.pdf_fwd = ;
			r.pdf_rev = ;
			r.eta = local_eta;
			r.roughness = sqrt(alpha_x*alpha_y);
		}
	}
}