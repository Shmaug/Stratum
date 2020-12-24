#pragma compile kernel Reduce3D
#pragma compile kernel Reduce2D
#pragma compile kernel Reduce1D

RWTexture3D<float4> Input3 : register(u0);
RWTexture3D<float4> Output3 : register(u1);

RWTexture2D<float4> Input2: register(u0);
RWTexture2D<float4> Output2 : register(u1);

RWTexture1D<float4> Input1 : register(u0);
RWTexture1D<float4> Output1 : register(u1);

[numthreads(4, 4, 4)]
void Reduce3D(uint3 index : SV_DispatchThreadID) {
  float4 total = 0;
  for (uint i = 0; i < 2; i++)
    for (uint j = 0; j < 2; j++)
      for (uint k = 0; k < 2; k++)
        total += Input3[2*index + uint3(i, j, k)];
  Output3[index] = total / 16;
}
[numthreads(8, 8, 1)]
void Reduce2D(uint3 index : SV_DispatchThreadID) {
  float4 total = 0;
  for (uint i = 0; i < 2; i++)
    for (uint j = 0; j < 2; j++)
      total += Input2[2*index.xy + uint2(i, j)];
  Output2[index.xy] = total / 4;
}

[numthreads(64, 1, 1)]
void Reduce1D(uint3 index : SV_DispatchThreadID) {
  float4 total = 0;
  for (uint i = 0; i < 2; i++)
    total += Input1[2*index.x + i];
  Output1[index.x] = total / 2;
}