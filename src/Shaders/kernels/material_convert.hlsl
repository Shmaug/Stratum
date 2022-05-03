#pragma compile dxc -spirv -T cs_6_7 -E alpha_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E shininess_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E from_metal_rough
#pragma compile dxc -spirv -T cs_6_7 -E from_diffuse_specular

Texture2D<float4> gDiffuse;
Texture2D<float4> gSpecular;
Texture2D<float3> gTransmittance;

RWTexture2D<float4> gPackedData; // diffuse (R), specular (G), roughness (B), transmission (A)

Texture2D<float> gInput;
RWTexture2D<float> gRoughness;

[[vk::constant_id(0)]] const bool gUseTransmittance = 0;

[numthreads(8,8,1)]
void alpha_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughness.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughness[index.xy] = sqrt(gInput[index.xy]);
}

[numthreads(8,8,1)]
void shininess_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gRoughness.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughness[index.xy] = sqrt(2 / (gInput[index.xy] + 2));
}

#include <common.h>

[numthreads(8,8,1)]
void from_metal_rough(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gPackedData.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	const float4 metallic_roughness = gSpecular[index.xy];
	const float occlusion = metallic_roughness.r;
	const float roughness = metallic_roughness.g;
	const float metallic = metallic_roughness.b;
	const float transmittance = gUseTransmittance ? saturate(luminance(gTransmittance[index.xy].rgb)*(1-metallic)) : 0;
	gPackedData[index.xy] = float4(lerp(1, 0.1, metallic), lerp(0.25, 1, metallic), roughness, transmittance);
}

[numthreads(8,8,1)]
void from_diffuse_specular(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gPackedData.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	const float ld = luminance(gDiffuse[index.xy].rgb);
	const float ls = luminance(gSpecular[index.xy].rgb);
	const float lt = gUseTransmittance ? luminance(gTransmittance[index.xy]) : 0;
	const float lsum = ld + ls + lt;
	gPackedData[index.xy] = float4(ld/lsum, ls/lsum, gRoughness[index.xy], lt/lsum);
}