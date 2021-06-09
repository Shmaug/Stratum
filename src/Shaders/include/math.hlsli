#ifndef MATH_H
#define MATH_H

#ifndef __cplusplus

#ifndef M_PI
#define M_PI (3.1415926535897932)
#endif
#ifndef M_1_PI
#define M_1_PI (1/M_PI)
#endif

struct quatf { float4 coeffs; };
#define QUATF_I quatf(float4(0,0,0,1))
inline quatf conj(quatf q) {
  quatf r;
  r.coeffs = float4(-q.coeffs.xyz, q.coeffs.w);
  return r;
}
inline quatf inverse(quatf q) {
  quatf r = conj(q);
  r.coeffs /= dot(q.coeffs, q.coeffs);
  return r;
}
inline quatf qmul(quatf q1, quatf q2) {
  quatf r;
  r.coeffs.x = (q1.coeffs.w * q2.coeffs.x) + (q1.coeffs.x * q2.coeffs.w) + (q1.coeffs.y * q2.coeffs.z) - (q1.coeffs.z * q2.coeffs.y);
  r.coeffs.y = (q1.coeffs.w * q2.coeffs.y) - (q1.coeffs.x * q2.coeffs.z) + (q1.coeffs.y * q2.coeffs.w) + (q1.coeffs.z * q2.coeffs.x);
  r.coeffs.z = (q1.coeffs.w * q2.coeffs.z) + (q1.coeffs.x * q2.coeffs.y) - (q1.coeffs.y * q2.coeffs.x) + (q1.coeffs.z * q2.coeffs.w);
  r.coeffs.w = (q1.coeffs.w * q2.coeffs.w) - (q1.coeffs.x * q2.coeffs.x) - (q1.coeffs.y * q2.coeffs.y) - (q1.coeffs.z * q2.coeffs.z);
  return r;
}
inline float3 rotate_vector(quatf q, float3 v) {
  return v + 2*cross(q.coeffs.xyz, cross(q.coeffs.xyz, v) + v*q.coeffs.w);
}

inline float3 homogeneous(float2 v) { return float3(v.xy,1); }
inline float4 homogeneous(float3 v) { return float4(v.xyz,1); }
inline float2 hnormalized(float3 v) { return v.xy/v.z; }
inline float3 hnormalized(float4 v) { return v.xyz/v.w; }

inline float max3(float3 x) { return max(max(x[0], x[1]), x[2]); }
inline float max4(float4 x) { return max(max(x[0], x[1]), max(x[2], x[3])); }
inline float min3(float3 x) { return min(min(x[0], x[1]), x[2]); }
inline float min4(float4 x) { return min(min(x[0], x[1]), min(x[2], x[3])); }

inline float pow2(float x) { return x*x; }
inline float pow3(float x) { return pow2(x)*x; }
inline float pow4(float x) { return pow2(x)*pow2(x); }
inline float pow5(float x) { return pow4(x)*x; }
inline float2 pow2(float2 x) { return x*x; }
inline float2 pow3(float2 x) { return pow2(x)*x; }
inline float2 pow4(float2 x) { return pow2(x)*pow2(x); }
inline float2 pow5(float2 x) { return pow4(x)*x; }
inline float3 pow2(float3 x) { return x*x; }
inline float3 pow3(float3 x) { return pow2(x)*x; }
inline float3 pow4(float3 x) { return pow2(x)*pow2(x); }
inline float3 pow5(float3 x) { return pow4(x)*x; }
inline float4 pow2(float4 x) { return x*x; }
inline float4 pow3(float4 x) { return pow2(x)*x; }
inline float4 pow4(float4 x) { return pow2(x)*pow2(x); }
inline float4 pow5(float4 x) { return pow4(x)*x; }

#endif

inline float3 hue_to_rgb(float hue) {
	float x = 6*hue;
	return saturate(float3(abs(x-3) - 1, 2 - abs(x-2), 2 - abs(x-4)));
}
inline float3 hsv_to_rgb(float3 hsv) {
	float3 rgb = hue_to_rgb(hsv[0]);
	return ((rgb - 1) * hsv[1] + 1) * hsv[2];
}
inline float3 rgb_to_hcv(float3 rgb) {
	// Based on work by Sam Hocevar and Emil Persson
	float4 P = (rgb[1] < rgb[2]) ? float4(rgb[2], rgb[1], -1, 2.f/3.f) : float4(rgb[1], rgb[2], 0, -1.f/3.f);
	float4 Q = (rgb[0] < P[0]) ? float4(P[0], P[1], P[3], rgb[0]) : float4(rgb[0], P[1], P[2], P[0]);
	float C = Q[0] - min(Q[3], Q[1]);
	float H = abs((Q[3] - Q[1]) / (6*C + 1e-6f) + Q[2]);
	return float3(H, C, Q[0]);
}
inline float3 rgb_to_hsv(float3 rgb) {
	float3 hcv = rgb_to_hcv(rgb);
	return float3(hcv[0], hcv[1] / (hcv[2] + 1e-6f), hcv[2]);
}

inline float2 ray_sphere(float3 origin, float3 dir, float3 p, float r) {
	float3 f = origin - p;
	float a = dot(dir, dir);
	float b = dot(f, dir);
	float3 l = a*f - dir*b;
	float det = pow2(a*r) - dot(l,l);
	if (det < 0) return float2(0);
	float inv_a = 1/a;
	det = sqrt(det * inv_a) * inv_a;
	return -b*inv_a + float2(-det, det);
}
inline float2 ray_box(float3 origin, float3 inv_dir, float3 mn, float3 mx) {
	float3 t0 = (mn - origin) * inv_dir;
	float3 t1 = (mx - origin) * inv_dir;
	return float2(max3(min(t0, t1)), min3(max(t0, t1)));
}

#endif