#pragma kernel Reduce

#pragma multi_compile DIM_2D DIM_3D
#pragma multi_compile FLOAT2 FLOAT3 FLOAT4 UINT UINT2 UINT3 UINT4

#if defined(UINT4)
#define TYPE uint4
#elif defined(UINT3)
#define TYPE uint3
#elif defined(UINT2)
#define TYPE uint2
#elif defined(UINT)
#define TYPE uint

#elif defined(FLOAT4)
#define TYPE float4
#elif defined(FLOAT3)
#define TYPE float3
#elif defined(FLOAT2)
#define TYPE float2
#else
#define TYPE float
#endif

#ifdef DIM_3D
[[vk::binding(0, 0)]] RWTexture3D<TYPE> Input : register(u0);
[[vk::binding(1, 0)]] RWTexture3D<TYPE> Output : register(u1);
#elif defined(DIM_2D)
[[vk::binding(0, 0)]] RWTexture2D<TYPE> Input : register(u0);
[[vk::binding(1, 0)]] RWTexture2D<TYPE> Output : register(u1);
#else
[[vk::binding(0, 0)]] RWTexture1D<TYPE> Input : register(u0);
[[vk::binding(1, 0)]] RWTexture1D<TYPE> Output : register(u1);
#endif

[numthreads(64, 1, 1)]
void Reduce(uint3 index : SV_DispatchThreadID) {
  TYPE total = 0;
  #ifdef DIM_3D
  for (uint i = 0; i < 2; i++)
    for (uint j = 0; j < 2; j++)
      for (uint k = 0; k < 2; k++)
        total += Input[2*index + uint3(i, j, k)];
  Output[index] = total / 16;
  #elif defined(DIM_2D)
  for (uint i = 0; i < 2; i++)
    for (uint j = 0; j < 2; j++)
      total += Input[2*index.xy + uint2(i, j)];
  Output[index.xy] = total / 4;
  #else
  for (uint i = 0; i < 2; i++)
    total += Input[2*index.x + i];
  Output[index.x] = total / 2;
  #endif
}