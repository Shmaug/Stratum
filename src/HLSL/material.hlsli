#ifndef MATERIAL_H
#define MATERIAL_H

#include "image_value.hlsli"

#ifdef __HLSL_VERSION

#ifndef Real
#define Real float
#define Vector3 float3
#define Spectrum float3
#endif

// When TransportDirection is TRANSPORT_TO_LIGHT, dir_in is the view direction, and dir_out is the light direction
#define TransportDirection bool
#define TRANSPORT_TO_LIGHT true
#define TRANSPORT_FROM_LIGHT false

template<typename Material>
inline Material load_material(uint address, const uint vertex) {
	Material material;
	return material;
}

// returns BSDF * cosine term
template<typename Material>
inline MaterialEvalRecord eval_material(const Material material, const Vector3 dir_in, const Vector3 dir_out, const uint vertex, const TransportDirection dir) {
	MaterialEvalRecord e;
	e.f = 0;
	e.pdfW = 0;
	return e;
}

template<typename Material>
inline MaterialSampleRecord sample_material(const Material material, const Vector3 rnd, const Vector3 dir_in, const uint vertex, const TransportDirection dir) {
	MaterialSampleRecord s;
	s.dir_out = 0;
	s.eta_roughness = 0;
	s.eval = eval_material(material, dir_in, s.dir_out, vertex, dir);
	return s;
}

template<typename Material> inline Spectrum eval_material_albedo  (const Material material, const uint vertex) { return 0; }
template<typename Material> inline MaterialEvalRecord eval_material_emission(const Material material, const Vector3 dir_out, const uint vertex) {
	MaterialEvalRecord e;
	e.f = 0;
	e.pdfW = 0;
	return e;
}
template<typename Material> inline MaterialSampleRecord sample_material_emission(const Material material, const Vector3 rnd, const Vector3 dir_in, const uint vertex) {
	MaterialSampleRecord s;
	s.dir_out = 0;
	s.eta_roughness = 0;
	s.eval = eval_material_emission(material, dir_in, s.dir_out, vertex);
	return s;
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

#ifdef __HLSL_VERSION

inline MaterialEvalRecord load_material_and_eval_emission(const uint address, const float3 dir_out, const uint vertex) {
	const uint type = gMaterialData.Load(address);
	if (type == BSDFType::eEmissive)
		return eval_material_emission(load_material<Emissive>(address + 4, vertex), dir_out, vertex);
	else if (type == BSDFType::eEnvironment)
		return eval_material_emission(load_material<Environment>(address + 4, vertex), dir_out, vertex);
	else {
		MaterialEvalRecord r;
		r.f = 0;
		r.pdfW = 0;
		return r;
	}
}

#endif // __HLSL_VERSION

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