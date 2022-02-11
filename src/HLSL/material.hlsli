#ifndef MATERIAL_H
#define MATERIAL_H

#include "image_value.hlsli"

#ifdef __HLSL_VERSION

enum class TransportDirection { eToLight, eToView };
struct BSDFEvalRecord {
    float3 f;
    float pdfW;
};
struct BSDFSampleRecord {
    float3 dir_out;
    // The index of refraction ratio. Set to 0 if it's not a transmission event.
    float eta;
    BSDFEvalRecord eval;
};
interface BSDF {
  // samples an outgoing direction. Also returns the index of refraction
  // Returns 0 if the sampling failed (e.g., if the incoming direction is invalid).
  // If dir == TO_LIGHT, incoming direction is the view direction and we're sampling for the light direction.
  BSDFSampleRecord sample(const float3 rnd, const float3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight);
  // outputs the BSDF times the cosine between outgoing direction and the shading normal, evaluated at a point.
  // When the transport direction is towards the lights, dir_in is the view direction, and dir_out is the light direction.
  BSDFEvalRecord eval(const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight);
  float3 eval_emission(const PathVertexGeometry);
  float3 eval_albedo(const PathVertexGeometry);
  void load(ByteAddressBuffer bytes, inout uint address);
};

#endif

#ifdef __cplusplus
struct BSDF {};
#endif

#include "bsdfs/lambertian.hlsli"
#include "bsdfs/emissive.hlsli"
#include "bsdfs/environment.hlsli"
#include "bsdfs/roughplastic.hlsli"
#include "bsdfs/roughdielectric.hlsli"

#define FOR_EACH_BSDF_TYPE(FN) \
  FN ( Lambertian ) \
  FN ( Emissive ) \
  FN ( Environment ) \
  FN ( RoughPlastic ) \
  FN ( RoughDielectric )
  /* Append BSDF types here */

#define ENUMIFY(T) e##T ,
enum BSDFType : uint { FOR_EACH_BSDF_TYPE( ENUMIFY ) eBSDFTypeCount };
#undef ENUMIFY

#ifdef __cplusplus

#define LISTIFY(T) T,
using Material = variant<
  FOR_EACH_BSDF_TYPE( LISTIFY )
  nullptr_t
>;
#undef LISTIFY

inline void store_material(ByteAppendBuffer& bytes, ImagePool& images, const Material& material) {
  bytes.Append(material.index() & 0xFF);
  #define STORE_MATERIAL_CASE(BSDF_T) \
    case e##BSDF_T: \
      get<BSDF_T>(material).store(bytes, images); \
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

#ifdef __HLSL_VERSION

#define BSDF_SWITCH(FN) \
  const uint type = data.Load(address)&0xFF; \
  address += 4; \
  switch (type) { \
    default: \
    FOR_EACH_BSDF_TYPE( FN ) \
  }

inline BSDFSampleRecord sample_material(ByteAddressBuffer data, uint address, const float3 rnd, const float3 dir_in, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
  #define FN(BSDF_T) \
  case BSDFType::e##BSDF_T : { \
    BSDF_T bsdf; \
    bsdf.load(data, address); \
    return bsdf.sample(rnd, dir_in, vertex, dir); \
  }
  BSDF_SWITCH( FN )
  #undef FN
}

inline BSDFEvalRecord eval_material(ByteAddressBuffer data, uint address, const float3 dir_in, const float3 dir_out, const PathVertexGeometry vertex, const TransportDirection dir = TransportDirection::eToLight) {
  #define FN(BSDF_T) \
  case BSDFType::e##BSDF_T : { \
    BSDF_T bsdf; \
    bsdf.load(data, address); \
    return bsdf.eval(dir_in, dir_out, vertex, dir); \
  }
  BSDF_SWITCH( FN )
  #undef FN
}

inline float3 eval_material_albedo(ByteAddressBuffer data, uint address, const PathVertexGeometry vertex) {
  #define FN(BSDF_T) \
  case BSDFType::e##BSDF_T : { \
    BSDF_T bsdf; \
    bsdf.load(data, address); \
    return bsdf.eval_albedo(vertex); \
  }
  BSDF_SWITCH( FN )
  #undef FN
}

inline float3 eval_material_emission(ByteAddressBuffer data, uint address, const PathVertexGeometry vertex) {
  #define FN(BSDF_T) \
  case BSDFType::e##BSDF_T : { \
    BSDF_T bsdf; \
    bsdf.load(data, address); \
    return bsdf.eval_emission(vertex); \
  }
  BSDF_SWITCH( FN )
  #undef FN
}

#undef BSDF_SWITCH

#endif // __HLSL_VERSION

#endif