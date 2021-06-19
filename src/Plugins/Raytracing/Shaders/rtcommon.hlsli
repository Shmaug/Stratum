#ifndef RT_COMMON_H
#define RT_COMMON_H

#define TEXTURE_COUNT 64

struct RTMaterial {
    float4 Emission;
    float3 BaseColor;
    float Roughness;
    float3 Absorption;
    float Metallic;
    float3 Scattering;
    float Transmission;
    float TransmissionRoughness;
    uint pad[3];

    float IndexOfRefraction; 
    uint BaseColorTexture;
    uint RoughnessTexture;
    uint NormalTexture;
    float4 TextureST;
};
struct RTLight {
	uint PrimitiveIndex;
	uint MaterialIndex;
};
struct BvhNode {
	float3 Min;
	uint StartIndex;
	float3 Max;
	uint RightOffset; // 1st child is at node[index + 1], 2nd child is at node[index + RightOffset]
	uint PrimitiveCount;
	uint pad[3];
};

#endif