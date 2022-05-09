struct Material {
	Spectrum color;
	Real diffuse_reflectance;
	Spectrum emission;
	Real specular_reflectance;
	Real specular_transmittance;
	Real alpha;
	Real eta;

	SLANG_MUTATING
	inline void load_and_sample(uint address, const float2 uv, const float uv_screen_size) {
		color = sample_image(load_image_value3(address), uv, uv_screen_size);
		const float4 packed = sample_image(load_image_value4(address), uv, uv_screen_size);
		diffuse_reflectance = packed.r;
		specular_reflectance = packed.g;
		alpha = pow2(max(gMinRoughness, packed.b));
		specular_transmittance = packed.a;
		emission = sample_image(load_image_value3(address), uv, uv_screen_size);
		eta = gMaterialData.Load<float>(address);
	}

	inline float specular_weight() { return specular_reflectance / (specular_reflectance + diffuse_reflectance); }

	inline void eval_lambertian(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (dir_in.z <= 0 || dir_out.z <= 0) {
			r.f = 0;
			r.pdf_fwd = 0;
			r.pdf_rev = 0;
		} else {
			r.f = dir_out.z * color / M_PI;
			r.pdf_fwd = cosine_hemisphere_pdfW(dir_out.z);
			r.pdf_rev = cosine_hemisphere_pdfW(dir_in.z);
		}
	}
	inline void sample_lambertian(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		if (dir_in.z < 0) {
			beta = 0;
			r.pdf_fwd = 0;
			r.pdf_rev = 0;
			r.eta = 0;
			r.roughness = 1;
		} else {
			r.pdf_rev = cosine_hemisphere_pdfW(dir_in.z);
			r.dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
			r.pdf_fwd = cosine_hemisphere_pdfW(r.dir_out.z);
			const Spectrum f = r.dir_out.z * color / M_PI;
			beta *= f / r.pdf_fwd;
			r.eta = 0;
			r.roughness = 1;
		}
	}

	inline void eval_roughplastic(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const Vector3 half_vector, const bool adjoint) {
		// We first account for the dielectric layer.

		// Fresnel equation determines how much light goes through,
		// and how much light is reflected for each wavelength.
		// Fresnel equation is determined by the angle between the (micro) normal and
		// both incoming and outgoing directions (dir_out & dir_in).
		// However, since they are related through the Snell-Descartes law,
		// we only need one of them.
		const Real F_i = fresnel_dielectric(dot(half_vector, dir_in ), eta);
		const Real F_o = fresnel_dielectric(dot(half_vector, dir_out), eta);
		const Real D = GTR2(half_vector.z, alpha); // "Generalized Trowbridge Reitz", GTR2 is equivalent to GGX.
		const Real G_in  = smith_masking_gtr2(dir_in, alpha);
		const Real G_out = smith_masking_gtr2(dir_out, alpha);

		const Real spec_contrib = specular_reflectance * ((G_in * G_out) * F_o * D) / (4 * dir_in.z * dir_out.z);

		// Next we account for the diffuse layer.
		// In order to reflect from the diffuse layer,
		// the photon needs to bounce through the dielectric layers twice.
		// The transmittance is computed by 1 - fresnel.
		const Real diffuse_contrib = diffuse_reflectance * (1 - F_o) * (1 - F_i) / M_PI;

		r.f = color * (spec_contrib + diffuse_contrib) * dir_out.z;

		if (specular_reflectance + diffuse_reflectance <= 0) {
			r.pdf_fwd = 0;
			r.pdf_rev = 0;
		} else {
			// VNDF sampling importance samples smith_masking(cos_theta_in) * GTR2(cos_theta_h, alpha) * cos_theta_out
			// (4 * cos_theta_v) is the Jacobian of the reflectiokn
			// For the diffuse lobe, we importance sample cos_theta_out
			const float spec_prob = specular_weight();
			r.pdf_fwd = lerp(cosine_hemisphere_pdfW(dir_out.z), (G_in  * D) / (4 * dir_in.z ), spec_prob);
			r.pdf_rev = lerp(cosine_hemisphere_pdfW(dir_in.z),  (G_out * D) / (4 * dir_out.z), spec_prob);
		}
	}
	inline void sample_roughplastic(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		Vector3 half_vector;
		if (rnd.z < specular_weight()) {
			half_vector = sample_visible_normals(dir_in, alpha, rnd.xy);
			r.dir_out = normalize(-dir_in + 2 * dot(dir_in, half_vector) * half_vector);
			r.roughness = sqrt(alpha);
		} else {
			r.dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
			half_vector = normalize(dir_in + r.dir_out);
			r.roughness = 1;
		}
		r.eta = 0;
		MaterialEvalRecord f;
		eval_roughplastic(f, dir_in, r.dir_out, half_vector, adjoint);
		beta *= f.f / f.pdf_fwd;
		r.pdf_fwd = f.pdf_fwd;
		r.pdf_rev = f.pdf_rev;
	}

	inline void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		eval_roughplastic(r, dir_in, dir_out, normalize(dir_in + dir_out), adjoint);
	}

	inline void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		sample_roughplastic(r, rnd, dir_in, beta, adjoint);
	}
};