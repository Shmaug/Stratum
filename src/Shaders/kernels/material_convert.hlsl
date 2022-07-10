#if 0
#pragma compile dxc -spirv -T cs_6_7 -E alpha_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E shininess_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E from_gltf_pbr
#pragma compile dxc -spirv -T cs_6_7 -E from_diffuse_specular
#endif

#include "../common.h"

Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;
Texture2D<float3> gTransmittance;
Texture2D<float> gRoughness;

RWTexture2D<float4> gDiffuseRoughness; // diffuse (RGB), roughness (A)
RWTexture2D<float4> gSpecularTransmission; // specular (RGB), transmission (A)

Texture2D<float> gInput;
RWTexture2D<float> gRoughnessRW;

#ifdef __SLANG__
#ifndef gUseDiffuse
#define gUseDiffuse 0
#endif
#ifndef gUseSpecular
#define gUseSpecular 0
#endif
#ifndef gUseTransmittance
#define gUseTransmittance 0
#endif
#ifndef gUseRoughness
#define gUseRoughness 0
#endif
#else
[[vk::constant_id(0)]] const bool gUseDiffuse = 0;
[[vk::constant_id(1)]] const bool gUseSpecular = 0;
[[vk::constant_id(2)]] const bool gUseTransmittance = 0;
[[vk::constant_id(3)]] const bool gUseRoughness = 0;
#endif

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(8,8,1)]
void alpha_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughnessRW.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughnessRW[index.xy] = saturate(sqrt(gInput[index.xy]));
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(8,8,1)]
void shininess_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughnessRW.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughnessRW[index.xy] = saturate(sqrt(2 / (gInput[index.xy] + 2)));
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(8,8,1)]
void from_gltf_pbr(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gSpecularTransmission.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	const float4 metallic_roughness = gUseSpecular ? gSpecular[index.xy] : float4(1,0.5,0,0);
	const float occlusion = metallic_roughness.r;
	const float roughness = metallic_roughness.g;
	const float metallic = metallic_roughness.b;
	const float transmittance = gUseTransmittance ? saturate(luminance(gTransmittance[index.xy].rgb)*(1-metallic)) : 0;
	const float3 base_color = gUseDiffuse ? gDiffuse[index.xy].rgb : 1;
	gDiffuseRoughness[index.xy] = float4(base_color*(1-metallic), roughness);
	gSpecularTransmission[index.xy] = float4(base_color*metallic, transmittance);
}

#ifdef __SLANG__
[shader("compute")]
#endif
[numthreads(8,8,1)]
void from_diffuse_specular(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gSpecularTransmission.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	const float3 diffuse = gUseDiffuse ? gDiffuse[index.xy].rgb : 0;
	const float3 specular = gUseSpecular ? gSpecular[index.xy].rgb : 0;
	const float3 transmittance = gUseTransmittance ? gTransmittance[index.xy].rgb : 0;
	gDiffuseRoughness[index.xy] = float4(gUseDiffuse ? diffuse : 1, gUseRoughness ? gRoughness[index.xy] : 1);
	gSpecularTransmission[index.xy] = float4(gUseSpecular ? specular : 1, gUseTransmittance ? luminance(transmittance) : 1);
}