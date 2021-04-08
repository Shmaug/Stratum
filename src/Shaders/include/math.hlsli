#ifndef MATH_H
#define MATH_H

#ifndef M_PI
#define M_PI (3.1415926535897932)
#endif
#ifndef M_1_PI
#define M_1_PI (1/M_PI)
#endif

#ifndef __cplusplus
inline float3 homogeneous(float2 v) { return float3(v.xy,1); }
inline float4 homogeneous(float3 v) { return float4(v.xyz,1); }
inline float2 hnormalized(float3 v) { return v.xy/v.z; }
inline float3 hnormalized(float4 v) { return v.xyz/v.w; }

struct quatf { float4 coeffs; };
#define QUATF_I { float4(0,0,0,1) }
quatf conj(quatf q) {
  quatf r;
  r.coeffs = float4(-q.coeffs.xyz, q.coeffs.w);
  return r;
}
quatf inverse(quatf q) {
  quatf r = conj(q);
  r.coeffs /= dot(q.coeffs, q.coeffs);
  return r;
}
quatf qmul(quatf q1, quatf q2) {
  quatf r;
  r.coeffs.x = (q1.coeffs.w * q2.coeffs.x) + (q1.coeffs.x * q2.coeffs.w) + (q1.coeffs.y * q2.coeffs.z) - (q1.coeffs.z * q2.coeffs.y);
  r.coeffs.y = (q1.coeffs.w * q2.coeffs.y) - (q1.coeffs.x * q2.coeffs.z) + (q1.coeffs.y * q2.coeffs.w) + (q1.coeffs.z * q2.coeffs.x);
  r.coeffs.z = (q1.coeffs.w * q2.coeffs.z) + (q1.coeffs.x * q2.coeffs.y) - (q1.coeffs.y * q2.coeffs.x) + (q1.coeffs.z * q2.coeffs.w);
  r.coeffs.w = (q1.coeffs.w * q2.coeffs.w) - (q1.coeffs.x * q2.coeffs.x) - (q1.coeffs.y * q2.coeffs.y) - (q1.coeffs.z * q2.coeffs.z);
  return r;
}
float3 rotate_vector(quatf q, float3 v) {
  return v + 2*cross(q.coeffs.xyz, cross(q.coeffs.xyz, v) + v*q.coeffs.w);
}
#endif

float max3(float3 x) { return max(max(x[0], x[1]), x[2]); }
float max4(float4 x) { return max(max(x[0], x[1]), max(x[2], x[3])); }
float min3(float3 x) { return min(min(x[0], x[1]), x[2]); }
float min4(float4 x) { return min(min(x[0], x[1]), min(x[2], x[3])); }

float pow2(float x) { return x*x; }
float pow3(float x) { return pow2(x)*x; }
float pow4(float x) { return pow2(x)*pow2(x); }
float pow5(float x) { return pow4(x)*x; }
float2 pow2(float2 x) { return x*x; }
float2 pow3(float2 x) { return pow2(x)*x; }
float2 pow4(float2 x) { return pow2(x)*pow2(x); }
float2 pow5(float2 x) { return pow4(x)*x; }
float3 pow2(float3 x) { return x*x; }
float3 pow3(float3 x) { return pow2(x)*x; }
float3 pow4(float3 x) { return pow2(x)*pow2(x); }
float3 pow5(float3 x) { return pow4(x)*x; }
float4 pow2(float4 x) { return x*x; }
float4 pow3(float4 x) { return pow2(x)*x; }
float4 pow4(float4 x) { return pow2(x)*pow2(x); }
float4 pow5(float4 x) { return pow4(x)*x; }

float acos_safe(float x) {
	if (x <= -1) return (float)M_PI/2;
	if (x >= 1) return 0;
	return acos(x);
}

float2 ray_sphere(float3 origin, float3 dir, float3 p, float r) {
	float3 f = origin - p;
	float a = dot(dir, dir);
	float b = dot(f, dir);
	float3 l = a*f - dir*b;
	float det = pow2(a*r) - dot(l,l);
	if (det < 0) return float2(0,0);
	float inv_a = 1/a;
	det = sqrt(det * inv_a) * inv_a;
	return -b*inv_a + float2(-det, det);
}
float2 ray_box(float3 origin, float3 inv_dir, float3 mn, float3 mx) {
	float3 t0 = (mn - origin) * inv_dir;
	float3 t1 = (mx - origin) * inv_dir;
	return float2(max3(min(t0, t1)), min3(max(t0, t1)));
}

#endif