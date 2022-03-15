#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "../scene.hlsli"

struct Emissive {
    ImageValue3 emission;
    
#ifdef __HLSL_VERSION
    template<bool TransportToLight, typename Real, typename Real3>
    inline BSDFEvalRecord<Real3> eval(const Real3 dir_in, const Real3 dir_out, const PathVertexGeometry vertex) {
        BSDFEvalRecord<Real3> r;
        r.f = 0;
        r.pdfW = 0;
        return r;
    }

    template<bool TransportToLight, typename Real, typename Real3>
    inline BSDFSampleRecord<Real3> sample(const Real3 rnd, const Real3 dir_in, const PathVertexGeometry vertex) {
        BSDFSampleRecord<Real3> r;
        r.dir_out = 0,
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    template<typename Real3> inline Real3 eval_albedo  (const PathVertexGeometry vertex) { return 0; }
    template<typename Real3> inline Real3 eval_emission(const PathVertexGeometry vertex) { return vertex.eval(emission); }

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