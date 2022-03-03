#pragma compile dxc -spirv -T cs_6_7 -E build_row_dist
#pragma compile dxc -spirv -T cs_6_7 -E sum_row_cdf
#pragma compile dxc -spirv -T cs_6_7 -E build_marginal_dist

#include "../common.hlsli"

Texture2D<float4> gImage;
RWTexture2D<float> gRowCDF; // height x (width + 1);
RWTexture2D<float> gRowPDF; // height x width;
RWTexture2D<uint> gConditionalLowerBound;
RWStructuredBuffer<float> gMarginalCDF; // height + 1;
RWStructuredBuffer<float> gMarginalPDF; // height;
RWStructuredBuffer<uint> gMarginalLowerBound;

inline float sample_image(const uint2 index, const float sinTheta) {
  return luminance(gImage[index].rgb) * sinTheta;
}

[numthreads(64,1,1)]
void build_row_dist(uint3 index : SV_DispatchThreadId) {
  uint width,height;
  gImage.GetDimensions(width,height);
  if (index.x >= height) return;

  const uint y = index.x;
  const float sinTheta = sin(M_PI * (y + 0.5) / height);

  gRowCDF[uint2(0, y)] = 0;
  for (uint x = 0; x < width; x++)
    gRowCDF[uint2(x + 1, y)] = gRowCDF[uint2(x, y)] + sample_image(uint2(x, y), sinTheta);
  
  const float integral = gRowCDF[uint2(width, y)];
  if (integral > 0) {
    const float norm = 1 / integral;
    for (uint x = 0; x < width; x++) {
      gRowPDF[uint2(x, y)] = sample_image(uint2(x, y), sinTheta) * norm;
      gRowCDF[uint2(x, y)] *= norm;
    }
  } else {
    // We shouldn't sample this row, but just in case we
    // set up a uniform distribution.
    const float norm = 1 / (float)width;
    for (uint x = 0; x < width; x++) {
      gRowPDF[uint2(x, y)] = norm;
      gRowCDF[uint2(x, y)] = x * norm;
    }
    gRowCDF[uint2(width, y)] = 1;
  }
}

[numthreads(1,1,1)]
void sum_row_cdf(uint3 index : SV_DispatchThreadId) {
  uint width,height;
  gImage.GetDimensions(width,height);
  
  // Now construct the marginal CDF for each column.
  gMarginalCDF[0] = 0;
  for (int y = 0; y < height; y++)
    gMarginalCDF[y + 1] = gMarginalCDF[y] + gRowCDF[uint2(width, y)];
}

[numthreads(64,1,1)]
void build_marginal_dist(uint3 index : SV_DispatchThreadId) {
  uint width,height;
  gImage.GetDimensions(width,height);
  if (index.x >= height) return;

  const uint y = index.x;
  
  const float total_values = gMarginalCDF[height];
  if (total_values > 0) {
    // Normalize
    const float norm = 1 / total_values;
    gMarginalCDF[y] *= norm;
    gMarginalPDF[y] = gRowCDF[uint2(width, y)] * norm;
  } else {
    // The whole thing is black...why are we even here?
    // Still set up a uniform distribution.
    const float norm = 1 / (float)height;
    gMarginalPDF[y] = norm;
    gMarginalCDF[y] = y * norm;
  }
  gRowCDF[uint2(width, y)] = 1;
  if (y == height-1)
    gMarginalCDF[height] = 1;
}