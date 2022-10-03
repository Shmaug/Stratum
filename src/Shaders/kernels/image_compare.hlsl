#pragma compile dxc -spirv -fspv-target-env=vulkan1.2 -T cs_6_7 -E main

#include <common.h>
#include <image_compare.h>

[[vk::constant_id(0)]] const uint gMode = 0;
[[vk::constant_id(1)]] const uint gQuantization = 16777216;

Texture2D<float4> gImage1;
Texture2D<float4> gImage2;
RWStructuredBuffer<uint> gOutput;

[numthreads(8,8,1)]
void main(uint3 index : SV_DispatchThreadID) {
	uint2 resolution;
	gImage1.GetDimensions(resolution.x, resolution.y);
	if (any(index.xy >= resolution)) return;

	const float3 c1 = gImage1[index.xy].rgb;
	const float3 c2 = gImage2[index.xy].rgb;

	float error = 0;
	switch (gMode) {
	case (uint)CompareMetric::eSMAPE:
		error = dot(1, abs(c1 - c2) / (abs(c1) + abs(c2)));
		break;
	case (uint)CompareMetric::eMSE:
		error = dot(1, pow2(c1 - c2));
		break;
	case (uint)CompareMetric::eAverage:
		error = dot(1, c1 - c2);
		break;
	}
	error /= 3*resolution.x*resolution.y;

	error = WaveActiveSum(error);

	if (WaveIsFirstLane()) {
		uint prev;
		const float valf = error*gQuantization;
		const uint val = valf;
		InterlockedAdd(gOutput[0], val, prev);
		if (valf >= 0xFFFFFFFF || 0xFFFFFFFF - val < prev) {
			gOutput[1] = 1;
		}
	}
}