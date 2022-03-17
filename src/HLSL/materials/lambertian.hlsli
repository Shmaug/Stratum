#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../scene.hlsli"

struct Lambertian {
    ImageValue3 reflectance;
    
#ifdef __HLSL_VERSION
	template<bool TransportToLight>
    inline BSDFEvalRecord eval(const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
        const Real ndotwo = max(0, dot(vertex.shading_normal, dir_out));
        BSDFEvalRecord r;
        if (dot(vertex.shading_normal, dir_in) < 0) {
            r.f = 0;
            r.pdfW = 0;
        } else {
            r.f = ndotwo * vertex.eval(reflectance) / M_PI;
            r.pdfW = cosine_hemisphere_pdfW(ndotwo);
        }
        return r;
    }

	template<bool TransportToLight>
    inline BSDFSampleRecord sample(const Vector3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex) {
        BSDFSampleRecord r;
        if (dot(dir_in, vertex.shading_normal) < 0) {
            r.eval.f = 0;
            r.eval.pdfW = 0;
            return r;
        }

        const Vector3 local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
        r.dir_out = vertex.shading_frame().to_world(local_dir_out);
        r.eta = 0;
        r.roughness = 1;
        r.eval.f = local_dir_out.z * vertex.eval(reflectance) / M_PI;
        r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
        return r;
    }

    inline Spectrum eval_albedo  (const PathVertexGeometry vertex) { return vertex.eval(reflectance); }
    inline Spectrum eval_emission(const PathVertexGeometry vertex) { return 0; }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        reflectance.load(bytes, address);
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus

    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        reflectance.store(bytes, resources);
    }
    inline void inspector_gui() {
        image_value_field("Reflectance", reflectance);
    }

#endif
};

#endif