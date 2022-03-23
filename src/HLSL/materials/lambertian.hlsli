#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../scene.hlsli"

struct Lambertian {
    ImageValue3 reflectance;
    
#ifdef __cplusplus
    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        reflectance.store(bytes, resources);
    }
    inline void inspector_gui() {
        image_value_field("Reflectance", reflectance);
    }
#endif

#ifdef __HLSL_VERSION
    inline void load(ByteAddressBuffer bytes, inout uint address) {
        reflectance.load(bytes, address);
    }

    inline BSDFEvalRecord eval(const Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
        const Real ndotwo = max(0, dot(gPathVertices[vertex].shading_normal, dir_out));
        BSDFEvalRecord r;
        if (dot(gPathVertices[vertex].shading_normal, dir_in) < 0) {
            r.f = 0;
            r.pdfW = 0;
        } else {
            r.f = ndotwo * sample_image(vertex, reflectance) / M_PI;
            r.pdfW = cosine_hemisphere_pdfW(ndotwo);
        }
        return r;
    }

    inline BSDFSampleRecord sample(const Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
        BSDFSampleRecord r;
        if (dot(dir_in, gPathVertices[vertex].shading_normal) < 0) {
            r.eval.f = 0;
            r.eval.pdfW = 0;
            return r;
        }

        const Vector3 local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
        r.dir_out = gPathVertices[vertex].shading_frame().to_world(local_dir_out);
        r.eta = 0;
        r.roughness = 1;
        r.eval.f = local_dir_out.z * sample_image(vertex, reflectance) / M_PI;
        r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
        return r;
    }

    inline Spectrum eval_albedo  (const uint vertex) { return sample_image(vertex, reflectance); }
#endif // __HLSL_VERSION
};

#ifdef __HLSL_VERSION
template<> inline BSDFEvalRecord eval_material(const Lambertian material, const Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
    const Real ndotwo = max(0, dot(gPathVertices[vertex].shading_normal, dir_out));
    BSDFEvalRecord r;
    if (dot(gPathVertices[vertex].shading_normal, dir_in) < 0) {
        r.f = 0;
        r.pdfW = 0;
    } else {
        r.f = ndotwo * sample_image(vertex, material.reflectance) / M_PI;
        r.pdfW = cosine_hemisphere_pdfW(ndotwo);
    }
    return r;
}

template<> inline BSDFSampleRecord sample_material(const Lambertian material, const Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
    BSDFSampleRecord r;
    if (dot(dir_in, gPathVertices[vertex].shading_normal) < 0) {
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    const Vector3 local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
    r.dir_out = gPathVertices[vertex].shading_frame().to_world(local_dir_out);
    r.eta = 0;
    r.roughness = 1;
    r.eval.f = local_dir_out.z * sample_image(vertex, material.reflectance) / M_PI;
    r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
    return r;
}

template<> inline Spectrum eval_material_albedo(const Lambertian material, const uint vertex) { return sample_image(vertex, material.reflectance); }
#endif

#endif