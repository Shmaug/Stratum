struct DisneyDiffuse {
	Spectrum base_color;
	Real roughness;
	Real subsurface;

	Spectrum Le() { return 0; }
	bool can_eval() { return true; }
	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (dir_in.z * dir_out.z < 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return; // No light through the surface
		}

		const Real ndotwi = abs(dir_in.z);
		const Real ndotwo = abs(dir_out.z);
		const Real hdotwo = abs(dot(normalize(dir_in + dir_out), dir_out));

		const Real FSS90 = roughness * hdotwo*hdotwo;
		const Real FD90 = 0.5 + 2*FSS90;
		const Real ndotwi5 = pow(1 - ndotwi, 5);
		const Real ndotwo5 = pow(1 - ndotwo, 5);
		const Real FDwi = 1 + (FD90 - 1) * ndotwi5;
		const Real FDwo = 1 + (FD90 - 1) * ndotwo5;

		const Spectrum f_base_diffuse = (base_color / M_PI) * FDwi * FDwo * ndotwo;

		const Real FSSwi = 1 + (FSS90 - 1) * ndotwi5;
		const Real FSSwo = 1 + (FSS90 - 1) * ndotwo5;
		const Spectrum f_subsurface = (1.25 * base_color / M_PI) * (FSSwi * FSSwo * (1 / (ndotwi + ndotwo) - 0.5) + 0.5) * ndotwo;

		r.f = (1 - subsurface)*f_base_diffuse + subsurface*f_subsurface;
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