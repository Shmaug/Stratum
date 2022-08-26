#include "disney_common.hlsli"
#include "disney_diffuse.hlsli"
#include "disney_metal.hlsli"
#include "disney_glass.hlsli"
#include "disney_clearcoat.hlsli"
#include "disney_sheen.hlsli"

struct DisneyMaterial : BSDF {
	DisneyMaterialData bsdf;

	SLANG_MUTATING
	void load(uint address, const float2 uv, const float uv_screen_size) {
		for (int i = 0; i < 2; i++)
			bsdf.data[i] = eval_image_value4(address, uv, uv_screen_size);
	}

	Spectrum Le() { return bsdf.base_color()*bsdf.emission(); }
	Spectrum albedo() { return bsdf.base_color(); }
	bool can_eval() { return any(bsdf.base_color() > 0); }
	bool is_specular() { return bsdf.metallic() > 0.999 && bsdf.roughness() < 1e-3; }

	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint) {
		disneydiffuse_eval(bsdf, r, dir_in, dir_out, adjoint);
	}
	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint) {
		disneydiffuse_sample(bsdf, r, rnd, dir_in, beta, adjoint);
	}
};