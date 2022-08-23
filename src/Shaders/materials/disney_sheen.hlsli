struct DisneySheen : BSDF {
	Spectrum base_color;
	Real sheen_tint;

	Spectrum Le() { return 0; }
	bool can_eval() { return true; }
	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (dir_in.z * dir_out.z < 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return; // No light through the surface
		}
		const Vector3 h = normalize(dir_in + dir_out);
		const Spectrum Ctint = luminance(base_color) > 1e-4 ? base_color/luminance(base_color) : 1;
		const Spectrum Csheen = (1 - sheen_tint) + sheen_tint*Ctint;
		r.f = Csheen * pow(1 - abs(dot(h, dir_out)), 5) * abs(dir_out.z);
		r.pdf_fwd = cosine_hemisphere_pdfW(abs(dir_out.z));
		r.pdf_rev = cosine_hemisphere_pdfW(abs(dir_in.z));
	}

	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		r.dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
		if (dir_in.z < 0) r.dir_out = -r.dir_out;
		r.pdf_fwd = cosine_hemisphere_pdfW(abs(dir_out.z));
		r.pdf_rev = cosine_hemisphere_pdfW(abs(dir_in.z));
		r.eta = 0;
		r.roughness = 1;

		MaterialEvalRecord e;
		eval(eval, dir_in, r.dir_out, adjoint);
		beta *= e.f / r.pdf_fwd;
	}
}