#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../scene.hlsli"

struct Lambertian {
#ifdef __cplusplus
    ImageValue3 reflectance;
    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        reflectance.store(bytes, resources);
    }
    inline void inspector_gui() {
        image_value_field("Reflectance", reflectance);
    }
#endif

#ifdef __HLSL__
    float3 reflectance;
#endif
};

#ifdef __HLSL__
template<> inline Lambertian load_material(uint address, const ShadingData shading_data) {
	Lambertian material;
    material.reflectance = sample_image(load_image_value3(address), shading_data);
	return material;
}

template<> inline bool material_has_bsdf<Lambertian>() { return true; }

template<> inline Spectrum eval_material_albedo(const Lambertian material, const ShadingData shading_data) { return material.reflectance; }

template<> inline MaterialEvalRecord eval_material(const Lambertian material, const Vector3 dir_in, const Vector3 dir_out, const ShadingData shading_data, const bool adjoint) {
    const Real n_dot_out = max(0, dot(shading_data.shading_normal(), dir_out));
    const Real n_dot_in  = max(0, dot(shading_data.shading_normal(), dir_in));
    MaterialEvalRecord r;
    if (n_dot_in <= 0 || n_dot_out <= 0) {
        r.f = 0;
        r.pdf_fwd = 0;
        r.pdf_rev = 0;
    } else {
        r.f = n_dot_out * material.reflectance / M_PI;
        r.pdf_fwd = cosine_hemisphere_pdfW(n_dot_out);
        r.pdf_rev = cosine_hemisphere_pdfW(n_dot_in);
    }
    return r;
}

template<> inline MaterialSampleRecord sample_material(const Lambertian material, const Vector3 rnd, const Vector3 dir_in, const ShadingData shading_data, const bool adjoint) {
    MaterialSampleRecord r;
    const float n_dot_in = dot(shading_data.shading_normal(), dir_in);
    if (n_dot_in < 0) {
        r.f = 0;
        r.pdf_fwd = 0;
        r.pdf_rev = 0;
        r.eta = 0;
        r.roughness = 1;
        return r;
    }
    const Vector3 local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
    r.dir_out = shading_data.to_world(local_dir_out);
    r.f = local_dir_out.z * material.reflectance / M_PI;
    r.pdf_fwd = cosine_hemisphere_pdfW(local_dir_out.z);
    r.pdf_rev = cosine_hemisphere_pdfW(n_dot_in);
    r.eta = 0;
    r.roughness = 1;
    return r;
}
#endif

#endif