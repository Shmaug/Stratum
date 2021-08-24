#pragma compile glslc -fshader-stage=comp -fentry-point=interleave
#pragma compile glslc -fshader-stage=comp -fentry-point=average2d
#pragma compile glslc -fshader-stage=comp -fentry-point=average3d

[[vk::binding(0)]] RWTexture2D<float4> gOutput2;
[[vk::binding(1)]] RWTexture2D<float4> gInput2;
[[vk::binding(2)]] RWTexture3D<float4> gOutput3;
[[vk::binding(3)]] RWTexture3D<float4> gInput3;

[[vk::binding(4)]] RWBuffer<float2> gOutputRG;
[[vk::binding(5)]] Buffer<float> gInputR;
[[vk::binding(6)]] Buffer<float> gInputG;

[[vk::push_constant]]
cbuffer {
  uint gWidth;
};

[numthreads(8, 8, 1)]
void interleave(uint3 index : SV_DispatchThreadID) {
  uint addr = index.x + index.y*gWidth;
  gOutputRG[addr] = float2(gInputR[addr], gInputG[addr]);
}

[numthreads(8, 8, 1)]
void average2d(uint3 index : SV_DispatchThreadID) {
  uint2 b = 2*index.xy;
  gOutput2[index.xy] = (gInput2[b + uint2(0,0)] + gInput2[b + uint2(1,0)] + gInput2[b + uint2(0,1)] + gInput2[b + uint2(1,1)]) / 4;
}

[numthreads(4, 4, 4)]
void average3d(uint3 index : SV_DispatchThreadID) {
  uint3 b = 2*index;
  gOutput3[index] = (
    gInput3[b + uint3(0,0,0)] + gInput3[b + uint3(1,0,0)] +
    gInput3[b + uint3(0,1,0)] + gInput3[b + uint3(1,1,0)] +
    gInput3[b + uint3(0,0,1)] + gInput3[b + uint3(1,0,1)] +
    gInput3[b + uint3(0,1,1)] + gInput3[b + uint3(1,1,1)]) / 8;
}