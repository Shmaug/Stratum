#pragma compile compute BakeVolume
#pragma compile compute BakeGradient

#include "common.hlsli"

RWTexture3D<float4> Output : register(t0);

[numthreads(4, 4, 4)]
void BakeVolume(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	Output[index] = SampleColor(index); // Same SampleColor function that is used by the volume shader
}

[numthreads(4, 4, 4)]
void BakeGradient(uint3 index : SV_DispatchThreadID) {
	if (any(index >= VolumeResolution)) return;
	Output[index] = float4(
		SampleColor(min(index + uint3(1, 0, 0), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(1, 0, 0), 0)).a,
		SampleColor(min(index + uint3(0, 1, 0), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(0, 1, 0), 0)).a,
		SampleColor(min(index + uint3(0, 0, 1), VolumeResolution-1)).a - SampleColor(max(int3(index) - int3(0, 0, 1), 0)).a, 0);
}
