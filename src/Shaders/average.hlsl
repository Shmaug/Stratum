#pragma compile compute average2d
#pragma compile compute average3d

RWTexture2D<float4> gOutput2 : register(u0);
RWTexture2D<float4> gInput2  : register(u1);
RWTexture3D<float4> gOutput3 : register(u2);
RWTexture3D<float4> gInput3  : register(u3);

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