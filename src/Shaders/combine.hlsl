#pragma compile compute combine

RWBuffer<float2> gOutput : register(u0);
Buffer<float> gInput0 : register(t1);
Buffer<float> gInput1 : register(t2);

struct PushConstants {
  uint Width;
};
[[vk::push_constant]] const PushConstants gPushConstants = {0};

[numthreads(8, 8, 1)]
void combine(uint3 index : SV_DispatchThreadID) {
  uint addr = index.x + index.y*gPushConstants.Width;
  gOutput[addr] = float2(gInput0[addr], gInput1[addr]);
}