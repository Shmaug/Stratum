#ifndef MATERIAL_H
#define MATERIAL_H

#include "image_value.hlsli"

#ifdef __HLSL__

#define gCosThetaEpsilon 1e-6

#ifndef Real
#define Real float
#define Vector3 float3
#define Spectrum float3
#endif



template<typename Material>
inline Material load_material(uint address, const ShadingData shading_data) {
	Material material;
	return material;
}

template<typename Material> inline bool material_has_bsdf() { return false; }

struct MaterialEvalRecord {
	float3 f;
	float pdf_fwd;
	float pdf_rev;
};
// returns BSDF * cosine term
template<typename Material>
inline MaterialEvalRecord eval_material(const Material material, const Vector3 dir_in, const Vector3 dir_out, const ShadingData shading_data, const bool adjoint) {
	MaterialEvalRecord e;
	e.f = 0;
	e.pdf_fwd = 0;
	e.pdf_rev = 0;
	return e;
}

struct MaterialSampleRecord {
	float3 dir_out;
	float3 f;
	float pdf_fwd;
	float pdf_rev;
	float eta; // index of refraction for transmission, otherwise 0
	float roughness;
};
template<typename Material>
inline MaterialSampleRecord sample_material(const Material material, const Vector3 rnd, const Vector3 dir_in, const ShadingData shading_data, const bool adjoint) {
	MaterialSampleRecord s;
	s.dir_out = 0;
	s.f = 0;
	s.pdf_fwd = 0;
	s.pdf_rev = 0;
	s.eta = 0;
	s.roughness = 0;
	return s;
}

template<typename Material> inline Spectrum eval_material_albedo(const Material material, const ShadingData shading_data) { return 0; }

struct EmissionEvalRecord {
	float3 f;
	float pdf;
};
template<typename Material> inline EmissionEvalRecord eval_material_emission(const Material material, const Vector3 dir_out, const ShadingData shading_data) {
	EmissionEvalRecord e;
	e.f = 0;
	e.pdf = 0;
	return e;
}

#endif

#include "materials/emissive.hlsli"
#include "materials/environment.hlsli"
#include "materials/het_volume.hlsli"
#include "materials/lambertian.hlsli"
#include "materials/roughplastic.hlsli"
#include "materials/roughdielectric.hlsli"

#define FOR_EACH_BSDF_TYPE(FN) 	\
	FN( Emissive )        		\
	FN( Environment )     		\
	FN( HeterogeneousVolume )	\
	FN( Lambertian ) 			\
	FN( RoughPlastic )			\
	FN( RoughDielectric )
	/* Append BSDF types here */

#define ENUMIFY(T) e ## T ,
enum BSDFType : uint { FOR_EACH_BSDF_TYPE( ENUMIFY ) eBSDFTypeCount };
#undef ENUMIFY

#ifdef __cplusplus

#define LISTIFY(T) T,
using Material = variant< FOR_EACH_BSDF_TYPE( LISTIFY ) nullptr_t >;
#undef LISTIFY

inline void store_material(ByteAppendBuffer& bytes, ResourcePool& resources, const Material& material) {
	bytes.Append(material.index() & 0xFF);
	#define STORE_MATERIAL_CASE(BSDF_T) \
	case e##BSDF_T: \
		get<BSDF_T>(material).store(bytes, resources); \
		break;

	switch (material.index()) {
	default:
	FOR_EACH_BSDF_TYPE( STORE_MATERIAL_CASE )
	}
	#undef STORE_BSDF_CASE
}

inline void material_inspector_gui_fn(Material& material) {
	#define MATERIAL_GUI_CASE(BSDF_T) \
	case e##BSDF_T: \
		ImGui::Text( #BSDF_T ); \
		get<BSDF_T>(material).inspector_gui(); \
		break;

	switch (material.index()) {
	default:
	FOR_EACH_BSDF_TYPE( MATERIAL_GUI_CASE )
	}
	#undef BSDF_GUI_CASE
}

#endif // __cplusplus

#endif