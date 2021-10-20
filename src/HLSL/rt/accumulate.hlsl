#pragma compile dxc -spirv -T cs_6_7 -E main

#include "rtscene.hlsli"

RWTexture2D<float4> gDst;
RWTexture2D<float4> gSamples;
Texture2D<float4> gNormalId;
Texture2D<float4> gPrevSamples;
Texture2D<float4> gPrevNormalId;
Texture2D<float2> gPrevUV;

TransformData gCameraToWorld;
ProjectionData gProjection;
TransformData gPrevCameraToWorld;
ProjectionData gPrevProjection;

[[vk::push_constant]] const struct {
	float gExposure;
	float gGamma;
} gPushConstants;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gSamples.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	float4 radiance = gSamples[index.xy];
	float4 normalId = gNormalId[index.xy];

	uint2 prevIndex = gPrevUV[index.xy] * resolution;
	if (all(prevIndex >= 0) && all(prevIndex < resolution)) {
		float4 prevRadiance = gPrevSamples[prevIndex.xy];
		float4 prevNormalId = gPrevNormalId[prevIndex.xy];

		if (all(prevNormalId.xyz == normalId.xyz)) {
			radiance.rgb = lerp(prevRadiance.rgb, radiance.rgb, .01);
			radiance.w = 1;
		}
	}

	gDst[index.xy] = float4(pow(gPushConstants.gExposure*radiance.rgb, 1/gPushConstants.gGamma), 1);
	gSamples[index.xy] = radiance;
}