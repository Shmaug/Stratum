#include "../microfacet.h"

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
Real R0(const Real eta) {
    const Real num = eta - 1;
    const Real denom = eta + 1;
    return (num*num) / (denom*denom);
}

struct DisneyMetal : BSDF {
	Spectrum base_color;
	Real alpha_x, alpha_y;

	void init() {
		Real roughness = 1;
		Real aniso = 0;
		const Real alpha = roughness*roughness;
		const Real aspect = sqrt(1 - 0.9 * aniso);
		alpha_x = max(0.0001, alpha / aspect);
		alpha_y = max(0.0001, alpha * aspect);
	}

	Real eval_pdf(const Vector3 dir_in, const Vector3 h) {
		return G1(alpha_x, alpha_y, dir_in) * Dm(alpha_x, alpha_y, h) / (4 * abs(dir_in.z));
	}

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (dir_in.z * dir_out.z < 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return; // No light through the surface
		}
		const Vector3 h = normalize(dir_in + dir_out);
		const Spectrum F = schlick_fresnel(base_color, abs(dot(h, dir_out)));
		r.f = F * Dm(alpha_x, alpha_y, h) * G1(alpha_x, alpha_y, dir_in) * G1(alpha_x, alpha_y, dir_out) / (4 * abs(dir_in.z));
		r.pdf_fwd = eval_pdf(dir_in , h);
		r.pdf_fwd = eval_pdf(dir_out, h);
	}

	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		const Vector3 h = sample_visible_normals(dir_in, alpha_x, alpha_y, rnd.xy);
		r.dir_out = reflect(-dir_in, h);
		r.pdf_fwd = eval_pdf(dir_in , h);
		r.pdf_fwd = eval_pdf(dir_out, h);
		r.eta = 0;
		r.roughness = sqrt(alpha_x*alpha_y);
	}
}