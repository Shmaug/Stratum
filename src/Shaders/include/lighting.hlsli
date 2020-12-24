#ifndef LIGHTING_H
#define LIGHTING_H

#define LIGHT_DISTANT 0
#define LIGHT_SPHERE 1
#define LIGHT_CONE 2

struct LightData {
	float4x4 ToLight;
	float3 Emission;
	uint Type_ShadowIndex; // { LightType (24 bits), ShadowIndex (8 bits) }
	float SpotAngleScale;
	float SpotAngleOffset;
	float2 ShadowCoordScale;
	float2 ShadowCoordOffset;
	float ShadowBias;
	uint pad;
};
struct EnvironmentData {
	float3 Ambient;
	uint LightCount;
};

#ifndef __cplusplus

Texture2D<float4> gEnvironmentTexture : register(t0, space0);
StructuredBuffer<LightData> gLights : register(t1, space0);
Texture2D<float> gShadowAtlas : register(t2, space0);
SamplerComparisonState gShadowSampler : register(s0, space0);
ConstantBuffer<EnvironmentData> gEnvironment : register(b0, space0);

#include "math.hlsli"

class BSDF {
	float3 Evaluate(float3 Li, float3 Lo, float3 position, float3 normal) {
		return 0;
	}
};

float3 Shade(BSDF bsdf, float3 position, float3 normal, float3 Lo) {
	float3 sum = 0;
	for (uint i = 0; i < gEnvironment.LightCount; i++) {
		LightData light = gLights[i];
		uint type = light.Type_ShadowIndex & 0x000000FF;
		uint shadowIndex = (light.Type_ShadowIndex & 0xFFFFFF00) >> 8;
		
		float4 lightCoord = mul(light.ToLight, float4(position, 1));

		float3 eval = 1;
		float pdf = 1;

		float3 Li = normalize(light.ToLight[2].xyz);
		if (type == LIGHT_DISTANT)
			eval = bsdf.Evaluate(Li, Lo, position, normal);
		else if (type == LIGHT_SPHERE) {
			Li = -light.ToLight[3].xyz - position;
			float d2 = dot(Li, Li);
			Li /= sqrt(d2);
			eval /= d2;
		} else if (type == LIGHT_CONE)
			eval *= pow2(saturate(-dot(Li, normalize(lightCoord.xyz)) * light.SpotAngleScale + light.SpotAngleOffset)); // angular attenuation

		lightCoord.xyz /= lightCoord.w;
		float2 uv = saturate(lightCoord.xy*.5+.5)*light.ShadowCoordScale + light.ShadowCoordOffset;
		eval *= gShadowAtlas.SampleCmpLevelZero(gShadowSampler, uv, lightCoord.z - light.ShadowBias);
		
		sum += light.Emission*eval/pdf;
	}
	return sum;
}

#endif

#endif