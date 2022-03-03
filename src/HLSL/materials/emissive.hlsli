#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "../scene.hlsli"

struct Emissive {
    ImageValue3 emission;
    
#ifdef __HLSL_VERSION
    template<typename Real, bool TransportToLight>
    inline BSDFEvalRecord<Real> eval(const vector<Real,3> dir_in, const vector<Real,3> dir_out, const PathVertexGeometry vertex) {
        BSDFEvalRecord<Real> r;
        r.f = 0;
        r.pdfW = 0;
        return r;
    }

    template<typename Real, bool TransportToLight>
    inline BSDFSampleRecord<Real> sample(const vector<Real,3> rnd, const vector<Real,3> dir_in, const PathVertexGeometry vertex) {
        BSDFSampleRecord<Real> r;
        r.dir_out = 0,
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    template<typename Real> inline vector<Real,3> eval_albedo  (const PathVertexGeometry vertex) { return 0; }
    template<typename Real> inline vector<Real,3> eval_emission(const PathVertexGeometry vertex) { return vertex.eval(emission); }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        emission.load(bytes, address);
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus

    inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
        emission.store(bytes, images);
    }
    inline void inspector_gui() {
        image_value_field("Emission", emission);
    }

#endif
};

#endif