#pragma compile dxc -spirv -T cs_6_7 -E alpha_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E shininess_to_roughness
#pragma compile dxc -spirv -T cs_6_7 -E pbr_to_diff_spec


Texture2D<float> gInput;
RWTexture2D<float> gRoughness;

Texture2D<float4> gInputDiffuse;
Texture2D<float4> gInputPackedData;
RWTexture2D<float4> gDiffuse;
RWTexture2D<float4> gSpecular;

[numthreads(8,8,1)]
void alpha_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gInput.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughness[index.xy] = sqrt(gInput[index.xy]);
}

[numthreads(8,8,1)]
void shininess_to_roughness(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gInput.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	gRoughness[index.xy] = sqrt(2 / (gInput[index.xy] + 2));
}

[numthreads(8,8,1)]
void pbr_to_diff_spec(uint3 index : SV_DispatchThreadId) {
	uint2 size;
	gInputDiffuse.GetDimensions(size.x, size.y);
	if (any(index.xy >= size)) return;
	const float4 base_color = gInputDiffuse[index.xy];
	const float4 packed = gInputPackedData[index.xy];
	const float occlusion = packed.r;
	const float roughness = packed.g;
	const float metallic = packed.b;
	gDiffuse[index.xy] = float4(base_color.rgb * (1 - metallic), 1);
	gSpecular[index.xy] = float4(base_color.rgb * metallic, roughness);
}