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

#ifdef __HLSL__
template<> inline Emissive load_material(uint address, const ShadingData shading_data) {
    Emissive material;
    material.emission = load_image_value3(address);
    return material;
}

template<> inline EmissionEvalRecord eval_material_emission(const Emissive material, const Vector3 dir_out, const ShadingData shading_data) {
    EmissionEvalRecord r;
    r.f = sample_image(material.emission, shading_data);
    r.pdf = 1;
    return r;
}

#endif // __HLSL__
#endif