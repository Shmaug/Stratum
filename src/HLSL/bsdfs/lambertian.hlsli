#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../scene.hlsli"

struct Lambertian {
    ImageValue3 reflectance;
    
#ifdef __HLSL_VERSION
    template<typename Real, bool TransportToLight>
    inline BSDFEvalRecord<Real> eval(const vector<Real,3> dir_in, const vector<Real,3> dir_out, const PathVertexGeometry vertex) {
        const Real ndotwo = max(0, dot(vertex.shading_normal, dir_out));
        BSDFEvalRecord<Real> r;
        if (dot(vertex.shading_normal, dir_in) < 0) {
            r.f = 0;
            r.pdfW = 0;
        } else {
            r.f = ndotwo * vertex.eval(reflectance) / M_PI;
            r.pdfW = cosine_hemisphere_pdfW(ndotwo);
        }
        return r;
    }

    template<typename Real, bool TransportToLight>
    inline BSDFSampleRecord<Real> sample(const vector<Real,3> rnd, const vector<Real,3> dir_in, const PathVertexGeometry vertex) {
        if (dot(dir_in, vertex.shading_normal) < 0) {
            BSDFSampleRecord<Real> r;
            r.eval.f = 0;
            r.eval.pdfW = 0;
            return r;
        }

        const vector<Real,3> local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
        BSDFSampleRecord<Real> r;
        r.dir_out = vertex.shading_frame().to_world(local_dir_out);
        r.eta = 0;
        r.roughness = 1;
        r.eval.f = local_dir_out.z * vertex.eval(reflectance) / M_PI;
        r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
        return r;
    }

    template<typename Real> inline vector<Real,3> eval_albedo  (const PathVertexGeometry vertex) { return vertex.eval(reflectance); }
    template<typename Real> inline vector<Real,3> eval_emission(const PathVertexGeometry vertex) { return 0; }

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