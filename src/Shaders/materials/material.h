#ifndef MATERIAL_H
#define MATERIAL_H

#ifdef __cplusplus
using Real = float;
using Vector3 = float3;
using Spectrum = float3;
#else
#ifndef Real
#define Real float
#define Vector3 float3
#define Spectrum float3
#endif
#endif

static const Real gMinRoughness = 1.0 / 64.0;
static const Real gCosThetaEpsilon = 1e-6;

#include "../scene.h"
#include "../image_value.h"
#include "../microfacet.h"

struct MaterialEvalRecord {
	Spectrum f;
	Real pdf_fwd;
	Real pdf_rev;
};
struct MaterialSampleRecord {
	Vector3 dir_out;
	Real pdf_fwd;
	Real pdf_rev;
	Real eta;
	Real roughness;
};

#ifdef __cplusplus
struct Material {
    ImageValue3 base_color;
    ImageValue4 packed_data; // diffuse (R), specular (G), roughness (B), transmission (A)
    ImageValue3 emission;
	float eta;

    inline void store(ByteAppendBuffer& bytes, MaterialResources& resources) const {
        base_color.store(bytes, resources);
        packed_data.store(bytes, resources);
        emission.store(bytes, resources);
		bytes.Appendf(eta);
    }
    inline void inspector_gui() {
        image_value_field("Base Color", base_color);
        image_value_field("Diffuse (R), Specular (G), Roughness (B), Transmission (A)", packed_data);
        image_value_field("Emission", emission);
		ImGui::DragFloat("Index of Refraction", &eta);
    }
};
#endif

#ifdef __HLSL__
#include "material.hlsli"
#endif

#endif