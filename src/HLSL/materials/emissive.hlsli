#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "../scene.hlsli"

struct Emissive {
    ImageValue3 emission;
    
#ifdef __HLSL_VERSION
    template<bool TransportToLight>
    inline BSDFEvalRecord eval(const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
        BSDFEvalRecord r;
        r.f = 0;
        r.pdfW = 0;
        return r;
    }

    template<bool TransportToLight>
    inline BSDFSampleRecord sample(const Vector3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex) {
        BSDFSampleRecord r;
        r.dir_out = 0,
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    inline Spectrum eval_albedo  (const PathVertexGeometry vertex) { return 0; }
    inline Spectrum eval_emission(const PathVertexGeometry vertex) { return vertex.eval(emission); }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        emission.load(bytes, address);
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus

    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        emission.store(bytes, resources);
    }
    inline void inspector_gui() {
        image_value_field("Emission", emission);
    }

#endif
};

#endif