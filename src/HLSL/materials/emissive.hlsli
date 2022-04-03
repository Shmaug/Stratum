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
};

#ifdef __HLSL_VERSION
template<> inline Emissive load_material(uint address, const uint vertex) {
    Emissive material;
    material.emission = load_image_value3(address);
    return material;
}
template<> inline MaterialEvalRecord eval_material_emission(const Emissive material, const Vector3 dir_out, const uint vertex) {
    MaterialEvalRecord r;
    r.f = sample_image(material.emission, vertex);
    r.pdfW = 1;
    return r;
}

#endif // __HLSL_VERSION
#endif