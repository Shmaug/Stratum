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
	void load(uint address, const float2 uv, const float uv_screen_size, inout uint packed_shading_normal, inout uint packed_tangent, const bool flip_bitangent) {
		for (int i = 0; i < DISNEY_DATA_N; i++)
			bsdf.data[i] = eval_image_value4(address, uv, uv_screen_size);

		address += 4; // alpha mask

		// normal map
		if (gUseNormalMaps) {
			const uint2 p = gMaterialData.Load<uint2>(address);
			ImageValue3 bump_img;
			bump_img.value = 1;
			bump_img.image_index = p.x;
			if (bump_img.has_image() && asfloat(p.y) > 0) {
				float3 bump = bump_img.eval(uv, uv_screen_size)*2-1;
				if (gFlipNormalMaps)
					bump.y = -bump.y;
				bump = normalize(float3(bump.xy * asfloat(p.y), bump.z > 0 ? bump.z : 1));

				float3 n = unpack_normal_octahedron(packed_shading_normal);
				float3 t = unpack_normal_octahedron(packed_tangent);

				n = normalize(t*bump.x + cross(n, t)*(flip_bitangent ? -1 : 1)*bump.y + n*bump.z);
				t = normalize(t - n*dot(n, t));

				packed_shading_normal = pack_normal_octahedron(n);
				packed_tangent        = pack_normal_octahedron(t);
			}
		}
	}

	SLANG_MUTATING
	void load(uint address, inout ShadingData sd) {
		load(address, sd.uv, sd.uv_screen_size, sd.packed_shading_normal, sd.packed_tangent, sd.flip_bitangent());
	}

	Spectrum Le() { return bsdf.base_color()*bsdf.emission(); }
	Spectrum albedo() { return bsdf.base_color(); }
	bool can_eval() { return bsdf.emission() <= 0 && any(bsdf.base_color() > 0); }
#ifdef FORCE_LAMBERTIAN
	bool is_specular() { return false; }

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (bsdf.emission() > 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}
		if (dir_in.z * dir_out.z <= 0) {
			r.f = 0;
			r.pdf_fwd = 0;
			r.pdf_rev = 0;
		} else {
			r.f = bsdf.base_color() * abs(dir_out.z) / M_PI;
			r.pdf_fwd = cosine_hemisphere_pdfW(abs(dir_out.z));
			r.pdf_rev = cosine_hemisphere_pdfW(abs(dir_in.z));
		}
	}
	Spectrum sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		if (bsdf.emission() > 0) {
			beta = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return 0;
		}
		r.dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
		r.pdf_fwd = cosine_hemisphere_pdfW(r.dir_out.z);
		r.pdf_rev = cosine_hemisphere_pdfW(abs(dir_in.z));
		if (dir_in.z < 0) r.dir_out.z = -r.dir_out.z;
		const Spectrum f = bsdf.base_color() * abs(r.dir_out.z) / M_PI;
		beta *= f / r.pdf_fwd;
		r.eta = 0;
		r.roughness = 1;
		return f;
	}
#else
	bool is_specular() { return (bsdf.metallic() > 0.999 || bsdf.transmission() > 0.999) && bsdf.roughness() <= 1e-2; }

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		if (bsdf.emission() > 0) {
			r.f = 0;
			r.pdf_fwd = r.pdf_rev = 0;
			return;
		}

		if (bsdf.transmission() > 0.25)
			disneyglass_eval(bsdf, r, dir_in, dir_out, adjoint);
		else if (bsdf.metallic() > 0.5 && bsdf.roughness() < 0.5)
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
		if (bsdf.transmission() > 0.25)
			return disneyglass_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else if (bsdf.metallic() > 0.5 && bsdf.roughness() < 0.5)
			return disneymetal_sample(bsdf, r, rnd, dir_in, beta, adjoint);
		else
			return disneydiffuse_sample(bsdf, r, rnd, dir_in, beta, adjoint);
	}
#endif
};