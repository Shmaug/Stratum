#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "../scene.hlsli"

struct Emissive {
    ImageValue3 emission;
    
#ifdef __cplusplus
    inline void store(ByteAppendBuffer& bytes, ResourcePool& resources) const {
        emission.store(bytes, resources);
    }
    inline void inspector_gui() {
        image_value_field("Emission", emission);
    }
#endif
#ifdef __HLSL_VERSION
    inline void load(ByteAddressBuffer bytes, inout uint address) {
        emission.load(bytes, address);
    }
#endif
};

#ifdef __HLSL_VERSION
template<> inline Spectrum eval_material_emission(const Emissive material, const PathVertexGeometry vertex) { return sample_image(vertex, material.emission); }

#endif // __HLSL_VERSION
#endif