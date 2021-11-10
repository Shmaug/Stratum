//=================================================================================================
//
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

// The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

#include "math.hlsli"

float3 ACES_fitted(float3 color) {
  static const float3x3 ACES_in = {
    { 0.59719, 0.35458, 0.04823 },
    { 0.07600, 0.90834, 0.01566 },
    { 0.02840, 0.13383, 0.83777 }
  };
  static const float3x3 ACES_out = {
    {  1.60475, -0.53108, -0.07367 },
    { -0.10208,  1.10813, -0.00605 },
    { -0.00327, -0.07276,  1.07602 }
  };
  // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
  color = mul(ACES_in, color);

  // Apply RRT and ODT
  float3 a = color * (color + 0.0245786f) - 0.000090537f;
  float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
  color = a / b;

  // ODT_SAT => XYZ => D60_2_D65 => sRGB
  return saturate(mul(ACES_out, color));
}