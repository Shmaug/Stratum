#pragma kernel Denoise

#include "rtcommon.h"

[[vk::binding(0, 0)]] RWTexture2D<float4> SpecularRadiance		    : register(u0);
[[vk::binding(1, 0)]] RWTexture2D<float4> Irradiance		          : register(u1);
[[vk::binding(2, 0)]] RWTexture2D<float4> Radiance		            : register(u2);
[[vk::binding(3, 0)]] RWTexture2D<float4> NormalT					        : register(u3);
[[vk::binding(4, 0)]] RWTexture2D<float2> Jitter					        : register(u4);
[[vk::binding(5, 0)]] RWTexture2D<float4> IrradianceHistory		    : register(t0);
[[vk::binding(6, 0)]] RWTexture2D<float4> SpecularRadianceHistory : register(t0);
[[vk::binding(7, 0)]] RWTexture2D<float4> NormalTHistory		      : register(t1);
[[vk::binding(8, 0)]] RWTexture2D<float2> JitterHistory		        : register(u3);

[[vk::push_constant]] cbuffer PushConstants : register(b0) {
	float4 CameraRotation;
	float3 CameraPosition;
	float FieldOfView;

	float4 CameraRotationHistory;
	float3 CameraPositionHistory;
	float FieldOfViewHistory;

	float2 Resolution;
};

#include <include/math.hlsli>

float3 Project(float2 uv, float aspect, float4 q, float fov) {
  float2 p = tan(.5 * fov) * float3(aspect, 1, 1) * float3(uv.x*2-1, 1-uv.y*2, 1);
  return normalize(qtRotate(q, float3(p, 1)));
}

[numthreads(8, 8, 1)]
void Denoise(uint3 index : SV_DispatchThreadID) {
  if (any(index.xy > uint2(Resolution))) return;

  float4 specularRadiance = SpecularRadiance[index.xy];
  float4 irradiance = Irradiance[index.xy];
  float4 normalT = NormalT[index.xy];
  
  if (normalT.w == 0 || isnan(normalT.w) || isinf(normalT.w)) {
    Radiance[index.xy] = float4(specularRadiance.rgb, 1);
    return;
  }

  float aspect = Resolution.x / Resolution.y;

  float2 jitter = Jitter[index.xy].xy;
	float2 uv = (float2(index.xy) + 0.5 + jitter) / Resolution;
  float3 view = Project(uv + jitter, aspect, CameraRotation, FieldOfView);
  float3 worldPos = CameraPosition + normalT.w * view;

  float3 lastViewPos = qtRotate(qtInvert(CameraRotationHistory), worldPos - CameraPositionHistory);
  float2 lastScreenPos = lastViewPos.xy / (lastViewPos.z * float2(aspect, 1) * tan(.5 * FieldOfViewHistory));
  float2 lastUV = float2(1,-1)*lastScreenPos.xy*.5 + .5;

  if (!any(lastUV < 0) && !any(lastUV > 1)) {
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int2 coord = clamp(int2(lastUV * Resolution) + int2(dx, dy), 0, int2(Resolution) - 1);

        float4 histNormalT = NormalTHistory[coord];
        if (histNormalT.w == 0 || isnan(histNormalT.w) || isinf(histNormalT.w)) continue;

        float2 histUV = (float2(coord) + 0.5 + JitterHistory[coord]) / Resolution;
        float3 histPos = CameraPositionHistory + histNormalT.w * Project(histUV, aspect, CameraRotationHistory, FieldOfViewHistory);
        
        float3 histView = (histPos - CameraPosition);
        float histDepth = length(histView);
        histView /= histDepth;

        float4 histIrradiance = IrradianceHistory[coord];
        float4 histSpecularRadiance = SpecularRadianceHistory[coord];

        float depthSimilarity = abs(1 - histDepth/normalT.w);
        float normalSimilarity = pow2(saturate(dot(histNormalT.xyz, normalT.xyz)));
        float viewSimilarity = pow2(saturate(dot(histView, view)));
        
        if (depthSimilarity < 0.01 && normalSimilarity > 0.99) {
          irradiance += histIrradiance;// * normalSimilarity * depthSimilarity;
          if (viewSimilarity > 0.99)
            specularRadiance += histSpecularRadiance;// * viewSimilarity * normalSimilarity * depthSimilarity;
        }
      }

  }

  Irradiance[index.xy] = irradiance;
  SpecularRadiance[index.xy] = specularRadiance;

  float4 totalRadiance = specularRadiance + irradiance;
  if (totalRadiance.w > 0) totalRadiance.rgb /= totalRadiance.w;
	Radiance[index.xy] = float4(totalRadiance.rgb, 1);
}