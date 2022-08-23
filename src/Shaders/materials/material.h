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
    ImageValue4 diffuse_roughness;  // diffuse (RGB), roughness (A)
    ImageValue4 specular_transmission; // specular (RGB), transmission (A)
    ImageValue3 emission;
	float eta;

    inline void store(ByteAppendBuffer& bytes, MaterialResources& resources) const {
        diffuse_roughness.store(bytes, resources);
        specular_transmission.store(bytes, resources);
        emission.store(bytes, resources);
		bytes.Appendf(eta);
    }
    inline void inspector_gui() {
        image_value_field("Diffuse (RGB) Roughness (A)", diffuse_roughness);
        image_value_field("Specular (RGB) Transmission (A)", specular_transmission);
        image_value_field("Emission", emission);
		ImGui::DragFloat("Index of Refraction", &eta);
    }
};
#endif

#ifdef __HLSL__

interface BSDF {
	Spectrum Le();
	bool can_eval();
	void eval(out MaterialEvalRecord r, const Vector3 dir_in, const Vector3 dir_out, const bool adjoint);
	void sample(out MaterialSampleRecord r, const Vector3 rnd, const Vector3 dir_in, inout Spectrum beta, const bool adjoint);
};

#include "material.hlsli"
#include "disney_material.hlsli"
#endif

#endif