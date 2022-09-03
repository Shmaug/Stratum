#include "bsdf.hlsli"
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

Real Dc(const Real alpha_g, const Real h_lz) {
    const Real alpha_g2 = alpha_g * alpha_g;
    return (alpha_g2 - 1) / (M_PI * log(alpha_g2)*(1 + (alpha_g2 - 1)*h_lz*h_lz));
}
Real Gc(const Vector3 w_l) {
    const Real wx = w_l.x*0.25;
    const Real wy = w_l.y*0.25;
    const Real lambda = (sqrt(1 + (wx*wx + wy*wy)/(w_l.z*w_l.z)) - 1) / 2;
    return 1 / (1 + lambda);
}

#include "disney_data.h"
#include "disney_diffuse.hlsli"
#include "disney_metal.hlsli"
#include "disney_glass.hlsli"
#include "disney_clearcoat.hlsli"
#include "disney_sheen.hlsli"

struct DisneyMaterial : BSDF {
	DisneyMaterialData bsdf;

	SLANG_MUTATING
	void load(uint address, const float2 uv, const float uv_screen_size) {
		for (int i = 0; i < DISNEY_DATA_N; i++)
			bsdf.data[i] = eval_image_value4(address, uv, uv_screen_size);
	}

	Spectrum Le() { return bsdf.base_color()*bsdf.emission(); }
	Spectrum albedo() { return bsdf.base_color(); }
	bool can_eval() { return bsdf.emission() <= 0 && any(bsdf.base_color() > 0); }
	bool is_specular() { return (bsdf.metallic() > 0.999 || bsdf.transmission() > 0.999) && bsdf.roughness() < 1e-3; }

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (bsdf.emission() > 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}
		if (bsdf.transmission() > 0)
			disneyglass_eval(bsdf, r, dir_in, dir_out, adjoint);
		else if (bsdf.metallic() > 0)
			disneymetal_eval(bsdf, r, dir_in, dir_out, adjoint);
		else
			disneydiffuse_eval(bsdf, r, dir_in, dir_out, adjoint);
	}
	Spectrum sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		if (bsdf.emission() > 0) {
			beta = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return 0;
		}
		if (bsdf.transmission() > 0)
			return disneyglass_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else if (bsdf.metallic() > 0)
			return disneymetal_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else
			return disneydiffuse_sample(bsdf, r, rnd, dir_in, beta, adjoint);
	}
};