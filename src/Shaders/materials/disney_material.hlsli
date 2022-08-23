Real Gc(const Vector3 w_l) {
    const Real wx = w_l.x*0.25;
    const Real wy = w_l.y*0.25;
    const Real lambda = (sqrt(1 + (wx*wx + wy*wy)/(w_l.z*w_l.z)) - 1) / 2;
    return 1 / (1 + lambda);
}
Real Dc(const Real alpha_g, const Real h_lz) {
    const Real alpha_g2 = alpha_g * alpha_g;
    return (alpha_g2 - 1) / (M_PI * log(alpha_g2)*(1 + (alpha_g2 - 1)*h_lz*h_lz));
}
Real R0(const Real eta) {
    const Real num = eta - 1;
    const Real denom = eta + 1;
    return (num*num) / (denom*denom);
}

struct DisneyMaterial : BSDF {
	float4 data[2];
	Spectrum base_color()  { return data[0].rgb; }
	Real emission()        { return data[0].a; }
	Real metallic()        { return data[1].r; }
	Real alpha()           { return data[1].g; }
	Real aniso()           { return data[1].b; }
	Real subsurface()      { return data[1].a; }
	Real clearcoat()       { return data[2].r; }
	Real clearcoat_gloss() { return data[2].g; }
	Real transmission()    { return data[2].b; }
	Real eta()             { return data[2].a; }

	SLANG_MUTATING
	inline void load(uint address, const float2 uv, const float uv_screen_size) {
		for (int i = 0; i < 2; i++)
			data[i] = eval_image_value4(address, uv, uv_screen_size);
	}

	inline Spectrum Le() { return base_color()*emission(); }
	inline bool can_eval() { return any(base_color() > 0); }

	// Clearcoat

	Real clearcoat_pdf(const Vector3 dir_out, const Vector3 h) {
		const Real alpha = (1 - clearcoat_gloss())*0.1 + clearcoat_gloss()*0.001;
		return Dc(alpha, h.z) * abs(h.z) / (4*abs(dot(h, dir_out)));
	}
	Spectrum eval_clearcoat(const Vector3 dir_in, const Vector3 dir_out, const Vector3 h) {
		const Real hdotwo = abs(dot(h, dir_out));
		const Real Fc = schlick_fresnel1(R0(1.5), hdotwo);
		const Real alpha_c = (1 - clearcoat_gloss())*0.1 + clearcoat_gloss()*0.001;
		return Fc * Dc(alpha_c, h.z) * Gc(dir_in) * Gc(dir_out) / (4 * abs(dir_in.z));
	}
	void eval_clearcoat(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (dir_in.z * dir_out.z < 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return; // No light through the surface
		}

		const Vector3 h  = normalize(dir_in + dir_out);
		r.f = eval_clearcoat(dir_in, dir_out, h);
		r.pdf_fwd = clearcoat_pdf(dir_out, h);
		r.pdf_rev = clearcoat_pdf(dir_in, h);
	}
	void sample_clearcoat(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		const Real alpha_c = (1 - clearcoat_gloss())*0.1 + clearcoat_gloss()*0.001;
		const Real alpha2 = alpha_c*alpha_c;
		const Real cos_phi = sqrt((1 - pow(alpha2, 1 - rnd.x)) / (1 - alpha2));
		const Real sin_phi = sqrt(1 - max(cos_phi*cos_phi, Real(0)));
		const Real theta = 2*M_PI * rnd.y;
		Vector3 h = Vector3(sin_phi*cos(theta), sin_phi*sin(theta), cos_phi);
		if (dir_in.z < 0) h = -h;

		r.dir_out = reflect(-dir_in, h);
		r.pdf_fwd = clearcoat_pdf(r.dir_out, h);
		beta *= eval_clearcoat(dir_in, r.dir_out, h) / r.pdf_fwd;
		r.pdf_rev = clearcoat_pdf(dir_in, h);
		r.eta = 1.5;
		r.roughness = alpha_c;
	}



	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
	}

	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
	}
};