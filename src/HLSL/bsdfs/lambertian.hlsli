#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../scene.hlsli"

struct Lambertian : BSDF {
    ImageValue3 reflectance;
    
#ifdef __HLSL_VERSION

    inline BSDFEvalRecord eval(const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        const float ndotwo = max(0, dot(vertex.shading_normal, dir_out));
        BSDFEvalRecord r;
        r.f = ndotwo * reflectance.eval(vertex) / M_PI;
        r.pdfW = cosine_hemisphere_pdfW(ndotwo);
        if (dot(vertex.geometry_normal, dir_out) < 0)
            r.pdfW = 0;
        return r;
    }

    inline BSDFSampleRecord sample(const float3 rnd, const float3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        if (dot(dir_in, vertex.geometry_normal) < 0) {
            BSDFSampleRecord r;
            r.eval.pdfW = 0;
            return r;
        }

        const float3 local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
        BSDFSampleRecord r;
        r.dir_out = vertex.shading_frame().to_world(local_dir_out);
        r.eta = 0;
        r.eval.f = local_dir_out.z * reflectance.eval(vertex) / M_PI;
        r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
        
        if (dot(vertex.geometry_normal, r.dir_out) < 0)
            r.eval.pdfW = 0;
        return r;
    }

    inline float3 eval_albedo(const PathVertexGeometry vertex) { return reflectance.eval(vertex); }
    inline float3 eval_emission(const PathVertexGeometry vertex) { return 0; }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        reflectance.load(bytes, address);
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus

    inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
        reflectance.store(bytes, images);
    }
    inline void inspector_gui() {
        image_value_field("Reflectance", reflectance);
    }

#endif
};

#endif