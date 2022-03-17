#ifndef MATERIAL_H
#define MATERIAL_H

#include "image_value.hlsli"

#ifdef __HLSL_VERSION

#ifndef Real
#define Real float
#define Vector3 float3
#define Spectrum float3
#endif

struct BSDFEvalRecord {
	Spectrum f;
	float pdfW;
};
struct BSDFSampleRecord {
	BSDFEvalRecord eval;
	Vector3 dir_out;
	// The index of refraction ratio. Set to 0 if it's not a transmission event.
	min16float eta;
	min16float roughness;
};

// for reference
struct NullBSDF {
	inline void load(ByteAddressBuffer bytes, inout uint address) {}

	// outputs the BSDF times the cosine between outgoing direction and the shading normal, evaluated at a point.
	// When the transport direction is towards the lights, dir_in is the view direction, and dir_out is the light direction.
	template<bool TransportToLight>
	inline BSDFEvalRecord eval(const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
		BSDFEvalRecord e;
		e.f = Spectrum(1,0,1);
		e.pdfW = 0;
		return e;
	}

	// samples an outgoing direction. Also returns the index of refraction
	// Returns 0 if the sampling failed (e.g., if the incoming direction is invalid).
	// If dir == TO_LIGHT, incoming direction is the view direction and we're sampling for the light direction.
	template<bool TransportToLight>
	inline BSDFSampleRecord sample(const Vector3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex) {
		BSDFSampleRecord s;
		s.dir_out = 0;
		s.eta = 0;
		s.roughness = 0;
		s.eval = eval<TransportToLight>(dir_in, s.dir_out, vertex);
		return s;
	}
	inline Spectrum eval_emission(const PathVertexGeometry vertex) { return 0; }
	inline Spectrum eval_albedo  (const PathVertexGeometry vertex) { return 0; }
};
#endif

#include "materials/lambertian.hlsli"
#include "materials/emissive.hlsli"
#include "materials/environment.hlsli"
#include "materials/roughplastic.hlsli"
#include "materials/roughdielectric.hlsli"
#include "materials/het_volume.hlsli"

#define FOR_EACH_BSDF_TYPE(FN) \
	FN( Lambertian ) 						 \
	FN( Emissive )        			 \
	FN( Environment )     			 \
	FN( HeterogeneousVolume )		 \
	FN( RoughPlastic )					 \
	FN( RoughDielectric )
	/* Append BSDF types here */

#define ENUMIFY(T) e ## T ,
enum BSDFType : uint { FOR_EACH_BSDF_TYPE( ENUMIFY ) eBSDFTypeCount };
#undef ENUMIFY

#ifdef __HLSL_VERSION

template<bool TransportToLight>
inline BSDFSampleRecord sample_material(ByteAddressBuffer data, uint address, const float3 rnd, const Vector3 dir_in, const PathVertexGeometry vertex) {
	#define CASE_FN(BSDF_T) \
	case e##BSDF_T: { \
		BSDF_T bsdf; \
		bsdf.load(data, address); \
		return bsdf.sample<TransportToLight>(rnd, dir_in, vertex); } \

	const uint type = data.Load(address); address += 4;
	switch (type) {
	default:
	FOR_EACH_BSDF_TYPE( CASE_FN )
	}
	#undef CASE_FN
}
template<bool TransportToLight>
inline BSDFEvalRecord eval_material(ByteAddressBuffer data, uint address, const Vector3 dir_in, const Vector3 dir_out, const PathVertexGeometry vertex) {
	#define CASE_FN(BSDF_T) \
	case e##BSDF_T: { \
		BSDF_T bsdf; \
		bsdf.load(data, address); \
		return bsdf.eval<TransportToLight>(dir_in, dir_out, vertex); } \

	const uint type = data.Load(address); address += 4;
	switch (type) {
	default:
	FOR_EACH_BSDF_TYPE( CASE_FN )
	}
	#undef CASE_FN
}

inline Spectrum eval_material_albedo(ByteAddressBuffer data, uint address, const PathVertexGeometry vertex) {
	#define CASE_FN(BSDF_T) \
	case e##BSDF_T: { \
		BSDF_T bsdf; \
		bsdf.load(data, address); \
		return bsdf.eval_albedo(vertex); } \

	const uint type = data.Load(address); address += 4;
	switch (type) {
	default:
	FOR_EACH_BSDF_TYPE( CASE_FN )
	}
	#undef CASE_FN
}

inline Spectrum eval_material_emission(ByteAddressBuffer data, uint address, const PathVertexGeometry vertex) {
	const uint type = data.Load(address); address += 4;
	if (type == BSDFType::eEmissive) {
		Emissive bsdf;
		bsdf.load(data, address);
		return bsdf.eval_emission(vertex);
	} else if (type == BSDFType::eEnvironment) {
		Environment bsdf;
		bsdf.load(data, address);
		return bsdf.eval_emission(vertex);
	} else
		return 0;
}

#undef BSDF_SWITCH
#undef BSDF_SWITCH_CASE

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