#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "../scene.hlsli"

struct Emissive : BSDF {
    ImageValue3 emission;
    
#ifdef __HLSL_VERSION

    inline BSDFEvalRecord eval(const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        BSDFEvalRecord r;
        r.f = 0;
        r.pdfW = 0;
        return r;
    }

    inline BSDFSampleRecord sample(const float3 rnd, const float3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
        BSDFSampleRecord r;
        r.dir_out = 0,
        r.eta = 0;
        r.eval.f = 0;
        r.eval.pdfW = 0;
        return r;
    }

    inline float3 eval_albedo(const PathVertexGeometry vertex) { return 0; }
    inline float3 eval_emission(const PathVertexGeometry vertex) { return emission.eval(vertex); }

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