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

	float error = 0;
	const float3 c1 = gImage1[index.xy].rgb;
	const float3 c2 = gImage2[index.xy].rgb;
	switch (gMode) {
	case (uint)ErrorMode::eMSELuminance:
		error = pow2(luminance(c1) - luminance(c2));
		break;
	case (uint)ErrorMode::eMSERGB:
		error = dot(1, pow2(c1 - c2));
		break;
	case (uint)ErrorMode::eAverageLuminance:
		error = (luminance(c1) - luminance(c2)) / (resolution.x*resolution.y);
		break;
	case (uint)ErrorMode::eAverageRGB:
		error = dot(1, c1 - c2) / (resolution.x*resolution.y*3);
		break;
	}

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