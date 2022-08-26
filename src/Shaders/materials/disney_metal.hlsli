Real disneymetal_eval_pdf(const Vector3 dir_in, const Vector3 h, const float2 alpha) {
	return G1(alpha.x, alpha.y, dir_in) * Dm(alpha.x, alpha.y, h) / (4 * abs(dir_in.z));
}

Spectrum disneymetal_eval(const Spectrum base_color, const Vector3 dir_in, const Vector3 dir_out, const Vector3 h, const float2 alpha) {
	const Spectrum F = schlick_fresnel3(base_color, abs(dot(h, dir_out)));
	return F * Dm(alpha.x, alpha.y, h) * G1(alpha.x, alpha.y, dir_in) * G1(alpha.x, alpha.y, dir_out) / (4 * abs(dir_in.z));
}

void disneymetal_eval(const DisneyMaterialData bsdf, out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
	if (dir_in.z * dir_out.z < 0) {
		r.f = 0;
		r.pdf_fwd = r.pdf_rev = 0;
		return; // No light through the surface
	}
	const Real aspect = sqrt(1 - 0.9 * bsdf.anisotropic());
	const float2 alpha = max(0.0001, float2(bsdf.alpha() / aspect, bsdf.alpha() * aspect));

	const Vector3 h = normalize(dir_in + dir_out);
	r.f = disneymetal_eval(bsdf.base_color(), dir_in, dir_out, h, alpha);
	r.pdf_fwd = disneymetal_eval_pdf(dir_in , h, alpha);
	r.pdf_fwd = disneymetal_eval_pdf(dir_out, h, alpha);
}

void disneymetal_sample(const DisneyMaterialData bsdf, out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
	const Real aspect = sqrt(1 - 0.9 * bsdf.anisotropic());
	const float2 alpha = max(0.0001, float2(bsdf.alpha() / aspect, bsdf.alpha() * aspect));

	const Vector3 h = sample_visible_normals(dir_in, alpha.x, alpha.y, rnd.xy);
	r.dir_out = reflect(-dir_in, h);
	r.pdf_fwd = disneymetal_eval_pdf(dir_in, h, alpha);
	r.pdf_fwd = disneymetal_eval_pdf(r.dir_out, h, alpha);
	r.eta = 0;
	r.roughness = bsdf.roughness();
	beta *= disneymetal_eval(bsdf.base_color(), dir_in, r.dir_out, h, alpha) / r.pdf_fwd;
}