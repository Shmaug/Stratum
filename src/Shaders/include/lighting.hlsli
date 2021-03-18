#ifndef LIGHTING_H
#define LIGHTING_H

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct LightData {
	float3 Emission;
	uint Type;
	float3 Direction;
	float SpotAngleScale;
	float3 Position;
	float SpotAngleOffset;
	float3 Right;
	float ShadowBias;
	float2 ShadowCoordScale;
	float2 ShadowCoordOffset;
};
struct EnvironmentData {
	float3 Ambient;
	uint LightCount;
};

struct MaterialData {
	float4 gTextureST;
	float4 gBaseColor;
	float3 gEmission;
	float gMetallic;
	float gRoughness;
	float gBumpStrength;
	float gAlphaCutoff;
	uint pad;
};

#ifndef __cplusplus

ConstantBuffer<EnvironmentData> gEnvironment 	: register(b1, space0);
StructuredBuffer<LightData> gLights						: register(t2, space0);
Texture2D<float> gShadowAtlas 								: register(t3, space0);
SamplerComparisonState gShadowSampler 				: register(s4, space0);
Texture2D<float4> gEnvironmentTexture 				: register(t5, space0);

#include "math.hlsli"

class DisneyBSDF {
	float3 diffuse;
	float3 specular;
	float3 emission;
	float roughness;
	float occlusion;
	float3 Evaluate(float3 Li, float3 Lo, float3 position, float3 normal) {
		return diffuse*saturate(dot(Li, normal)) + pow(specular*dot(Li, reflect(Lo,normal)), 250*(1-roughness)) + emission;
	}
};

float3 Shade(DisneyBSDF bsdf, float3 position, float3 normal, float3 Lo) {
	float3 sum = 0;
	uint addr = 0;

	for (uint i = 0; i < gEnvironment.LightCount; i++) {
		LightData light = gLights[i];
		
		float atten = 1;
		float3 Li = light.Direction;

		if (any(light.ShadowCoordScale)) {
			float3 lightCoord = position - light.Position;
			lightCoord = float3(dot(light.Right, lightCoord), dot(cross(light.Direction, light.Right), lightCoord), dot(light.Direction, lightCoord));
			atten *= gShadowAtlas.SampleCmpLevelZero(gShadowSampler, saturate(lightCoord.xy*.5+.5)*light.ShadowCoordScale + light.ShadowCoordOffset, lightCoord.z - light.ShadowBias);
		}
		
		if (light.Type > LIGHT_DIRECTIONAL) {
			Li = light.Position - position;
			float rcp_d = 1/length(Li);
			Li *= rcp_d;
			atten *= rcp_d*rcp_d; // distance attenuation
			if (light.Type == LIGHT_SPOT)
				atten *= pow2(saturate(dot(Li, light.Direction) * light.SpotAngleScale + light.SpotAngleOffset)); // angle attenuation
		}
		sum += atten * light.Emission * bsdf.Evaluate(Li, Lo, position, normal);
	}
	return sum;
}

#endif

#endif