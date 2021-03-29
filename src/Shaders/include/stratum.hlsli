#ifndef STRATUM_H
#define STRATUM_H

#include "dtypes.h"
#include "sampling.hlsli"

ConstantBuffer<CameraData> gCamera    : register(b0, space0);
StructuredBuffer<LightData> gLights		: register(t1, space0);
Texture2D<float> gShadowAtlas 				: register(t2, space0);
SamplerComparisonState gShadowSampler : register(s3, space0);

bool SampleLight(LightData light, float3 position, out float3 Le, out float3 Li) {
  float atten = 1;
  float3 lightCoord = hnormalize(mul(light.ToLight, homogeneous(position)));

	if (light.Flags & LIGHT_ATTEN_DISTANCE) {
		Li = light.Position - position;
		float atten = 1/dot(Li,Li);
		Li *= sqrt(atten);
	} else
    Li = light.Position; // LIGHT_ATTEN_DIRECTIONAL

  if (light.Flags & LIGHT_ATTEN_ANGULAR)
    atten *= pow2(saturate(lightCoord.z/length(lightCoord)) * light.SpotAngleScale + light.SpotAngleOffset);
    
	if (light.Flags & LIGHT_USE_SHADOWMAP)
		atten *= gShadowAtlas.SampleCmpLevelZero(gShadowSampler, saturate(lightCoord.xy*.5+.5)*light.ShadowCoordScale + light.ShadowCoordOffset, lightCoord.z - light.ShadowBias);

  Le = light.Emission * atten;
  return atten > 0;
}

#endif // STRATUM_H