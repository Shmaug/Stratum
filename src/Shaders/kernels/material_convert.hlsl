#if 0
#pragma compile dxc -spirv -T cs_6_7 -E alpha_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E shininess_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E from_gltf_pbr
#pragma compile dxc -spirv -T cs_6_7 -E from_diffuse_specular
#endif

#include "../common.h"
#include "../materials/disney_data.h"

// used by **_to_roughness kernels
Texture2D<float> gInput;
RWTexture2D<float> gRoughnessRW;

Texture2D<float4> gDiffuse; // also base color
Texture2D<float4> gSpecular;
Texture2D<float3> gTransmittance;
Texture2D<float> gRoughness;
RWTexture2D<float4> gOutput[DISNEY_DATA_N];

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

SLANG_SHADER("compute")
[numthreads(8,8,1)]
void alpha_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughnessRW.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughnessRW[index.xy] = saturate(sqrt(gInput[index.xy]));
}

SLANG_SHADER("compute")
[numthreads(8,8,1)]
void shininess_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughnessRW.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughnessRW[index.xy] = saturate(sqrt(2 / (gInput[index.xy] + 2)));
}

SLANG_SHADER("compute")
[numthreads(8,8,1)]
void from_gltf_pbr(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	if (gUseDiffuse) gDiffuse.GetDimensions(size.x, size.y);
	else if (gUseSpecular) gDiffuse.GetDimensions(size.x, size.y);
	else if (gUseTransmittance) gTransmittance.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;

	const float4 metallic_roughness = gUseSpecular ? gSpecular[index.xy] : 1;

	DisneyMaterialData m;
	for (uint i = 0; i < DISNEY_DATA_N; i++) m.data[i] = 1;

	m.base_color(gUseDiffuse ? gDiffuse[index.xy].rgb : 1);
	m.metallic(metallic_roughness.b);
	m.roughness(metallic_roughness.g);

	const float l = luminance(m.base_color());
	m.transmission(gUseTransmittance ? saturate(luminance(gTransmittance[index.xy].rgb)/(l > 0 ? l : 1)) : 0);

	for (uint j = 0; j < DISNEY_DATA_N; j++) gOutput[j][index.xy] = m.data[j];
}

SLANG_SHADER("compute")
[numthreads(8,8,1)]
void from_diffuse_specular(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	if (gUseDiffuse) gDiffuse.GetDimensions(size.x, size.y);
	else if (gUseSpecular) gDiffuse.GetDimensions(size.x, size.y);
	else if (gUseTransmittance) gTransmittance.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;

	DisneyMaterialData m;
	for (uint i = 0; i < DISNEY_DATA_N; i++) m.data[i] = 1;

	const float3 diffuse = gUseDiffuse ? gDiffuse[index.xy].rgb : 0;
	const float3 specular = gUseSpecular ? gSpecular[index.xy].rgb : 0;
	const float3 transmittance = gUseTransmittance ? gTransmittance[index.xy].rgb : 0;
	const float ld = luminance(diffuse);
	const float ls = luminance(specular);
	const float lt = luminance(transmittance);
	m.base_color((diffuse * ld + specular * ls + transmittance*lt) / (ls + ld + lt));
	if (gUseSpecular) m.metallic(saturate(ls/(ld + ls + lt)));
	if (gUseRoughness) m.roughness(gRoughness[index.xy]);
	if (gUseTransmittance) m.transmission(saturate(lt / (ld + ls + lt)));

	for (uint j = 0; j < DISNEY_DATA_N; j++) gOutput[j][index.xy] = m.data[j];
}