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

	/*
	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (bsdf.emission() > 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}

		const Real aspect = sqrt(1 - 0.9 * bsdf.anisotropic());
		const float2 alpha = max(0.0001, float2(bsdf.alpha() / aspect, bsdf.alpha() * aspect));
		const Real local_eta = dir_in.z < 0 ? 1/bsdf.eta() : bsdf.eta();

		const Vector3 h = (dir_in.z * dir_out.z > 0) ? normalize(dir_in + dir_out) : normalize(dir_in + dir_out * local_eta);

		const Real h_dot_in = dot(h, dir_in);
		const Real h_dot_out = dot(h, dir_out);
		const Real F = fresnel_dielectric(h_dot_in, local_eta);
		const Real D = Dm(alpha.x, alpha.y, h);
		const Real G_in  = G1(alpha.x, alpha.y, dir_in);
		const Real G_out = G1(alpha.x, alpha.y, dir_out);
		const Real G = G_in * G_out;

        const Real w_diffuse = (1 - bsdf.transmission()) * (1 - bsdf.metallic());
        const Real w_metal =  1 - bsdf.transmission() * (1 - bsdf.metallic());
        const Real w_glass = (1 - bsdf.metallic()) * bsdf.transmission();
		const Real inv_w_sum = 1 / (w_diffuse + w_glass);

		r.f = 0;
		r.pdf_fwd = r.pdf_rev = 0;
		if (dir_in.z * dir_out.z > 0) {
			r.f += w_diffuse * disneydiffuse_eval(bsdf, dir_in, dir_out);
			r.pdf_fwd += w_diffuse*inv_w_sum * cosine_hemisphere_pdfW(abs(dir_out.z));
			r.pdf_rev += w_diffuse*inv_w_sum * cosine_hemisphere_pdfW(abs(dir_in.z));

			r.f += w_glass * disneyglass_eval_reflect(bsdf.base_color(), F, D, G, dir_in.z);
			r.pdf_fwd += w_glass*inv_w_sum * disneyglass_refract_pdf(F, D, G_in, dir_in.z, h_dot_in, h_dot_out, local_eta);
			r.pdf_rev += w_glass*inv_w_sum * disneyglass_refract_pdf(fresnel_dielectric(h_dot_out, 1/local_eta), D, G_out, dir_out.z, h_dot_out, h_dot_in, 1/local_eta);
		} else {
			r.f += w_glass * disneyglass_eval_refract(bsdf.base_color(), F, D, G, dir_in.z, h_dot_in, h_dot_out, local_eta, adjoint);
			r.pdf_fwd += w_glass*inv_w_sum * disneyglass_refract_pdf(F, D, G_in, dir_in.z, h_dot_in, h_dot_out, local_eta);
			r.pdf_rev += w_glass*inv_w_sum * disneyglass_refract_pdf(fresnel_dielectric(h_dot_out, 1/local_eta), D, G_out, dir_out.z, h_dot_out, h_dot_in, 1/local_eta);
		}
	}
	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		if (bsdf.emission() > 0) {
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}

        const Real w_diffuse = (1 - bsdf.transmission()) * (1 - bsdf.metallic());
        const Real w_metal =  1 - bsdf.transmission() * (1 - bsdf.metallic());
        const Real w_glass = (1 - bsdf.metallic()) * bsdf.transmission();
		const Real inv_w_sum = 1 / (w_diffuse + w_glass);

		const Real local_eta = dir_in.z > 0 ? bsdf.eta() : 1 / bsdf.eta();
		const Real aspect = sqrt(1 - 0.9 * bsdf.anisotropic());
		const float2 alpha = max(0.0001, float2(bsdf.alpha() / aspect, bsdf.alpha() * aspect));
		const Vector3 h = sample_visible_normals(dir_in, alpha.x, alpha.y, rnd.xy);
		const Real h_dot_in = dot(h, dir_in);

		const Real F = fresnel_dielectric(h_dot_in, local_eta);
		const Real D = Dm(alpha.x, alpha.y, h);
		const Real G_in = G1(alpha.x, alpha.y, dir_in);

		Spectrum tmp;
		if (rnd.z < w_glass*inv_w_sum) {
			const Real h_dot_out_sq = 1 - (1 - h_dot_in * h_dot_in) / (local_eta * local_eta);
			if (h_dot_out_sq <= 0 || rnd.z <= F) {
				// Reflection
				r.dir_out = reflect(-dir_in, h);
				r.eta = 0;
			} else {
				// Refraction
				r.dir_out = refract(-dir_in, h, local_eta);
				r.eta = local_eta;
			}
		} else {
			r.dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
			if (dir_in.z < 0) r.dir_out = -r.dir_out;
			r.eta = 0;
		}

		r.roughness = bsdf.roughness();

		const Real G_out = G1(alpha.x, alpha.y, r.dir_out);
		const Real h_dot_out = dot(h, r.dir_out);

		Spectrum f = 0;
		r.pdf_fwd = r.pdf_rev = 0;
		if (dir_in.z * r.dir_out.z > 0) {
			f += w_diffuse * disneydiffuse_eval(bsdf, dir_in, r.dir_out);
			r.pdf_fwd += w_diffuse*cosine_hemisphere_pdfW(abs(r.dir_out.z));
			r.pdf_rev += w_diffuse*cosine_hemisphere_pdfW(abs(dir_in.z));

			f += w_glass * disneyglass_eval_reflect(bsdf.base_color(), F, D, G_in*G_out, dir_in.z);
			r.pdf_fwd += w_glass*disneyglass_reflect_pdf(F, D, G_in, dir_in.z);
			r.pdf_rev += w_glass*disneyglass_reflect_pdf(fresnel_dielectric(h_dot_out, local_eta), D, G_out, r.dir_out.z);
		} else {
			f += w_glass * disneyglass_eval_refract(bsdf.base_color(), F, D, G_in*G_out, dir_in.z, h_dot_in, h_dot_out, local_eta, adjoint);
			r.pdf_fwd += w_glass*disneyglass_refract_pdf(F, D, G_in, dir_in.z, h_dot_in, h_dot_out, local_eta);
			r.pdf_rev += w_glass*disneyglass_refract_pdf(fresnel_dielectric(h_dot_out, 1/local_eta), D, G_out, r.dir_out.z, h_dot_out, h_dot_in, 1/local_eta);
		}

		r.pdf_fwd *= inv_w_sum;
		r.pdf_rev *= inv_w_sum;

		beta *= f / r.pdf_fwd;
	}*/

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
	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		if (bsdf.emission() > 0) {
			beta = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}
		if (bsdf.transmission() > 0)
			disneyglass_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else if (bsdf.metallic() > 0)
			disneymetal_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else
			disneydiffuse_sample(bsdf, r, rnd, dir_in, beta, adjoint);
	}
};