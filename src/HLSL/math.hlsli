#ifndef MATH_H
#define MATH_H

#ifndef M_PI
#define M_PI (3.1415926535897932)
#endif
#ifndef M_1_PI
#define M_1_PI (1/M_PI)
#endif

struct quatf {
	float3 xyz;
	float w;
};
inline quatf make_quatf(float x, float y, float z, float w) {
	quatf q;
	q.xyz[0] = x;
	q.xyz[1] = y;
	q.xyz[2] = z;
	q.w = w;
	return q;
}
inline quatf make_quatf(float3 xyz, float w) {
	quatf q;
	q.xyz = xyz;
	q.w = w;
	return q;
}

inline quatf angle_axis(float angle, float3 axis) {
	return make_quatf(axis*sin(angle/2), cos(angle/2));
}

inline quatf normalize(quatf q) {
	float nrm = 1 / sqrt(q.xyz[0]*q.xyz[0] + q.xyz[1]*q.xyz[1] + q.xyz[2]*q.xyz[2] + q.w*q.w);
	return make_quatf(q.xyz*nrm, q.w*nrm);
}
inline quatf inverse(quatf q) { return normalize( make_quatf(-q.xyz, q.w) ); }
inline quatf qmul(quatf q1, quatf q2) {
	return make_quatf(
		q1.w*q2.xyz + q2.w*q1.xyz + cross(q1.xyz,q2.xyz),
		q1.w*q2.w - (q1.xyz[0]*q2.xyz[0] + q1.xyz[1]*q2.xyz[1] + q1.xyz[2]*q2.xyz[2]) );
}
inline float3 rotate_vector(quatf q, float3 v) {
  return qmul(q, qmul(make_quatf(v,0), inverse(q))).xyz;
}
inline quatf slerp(quatf a, quatf b, float t) {
	float cosHalfAngle = a.w * b.w + a.xyz[0]*b.xyz[0] + a.xyz[1]*b.xyz[1] + a.xyz[2]*b.xyz[2];

	if (cosHalfAngle >= 1 || cosHalfAngle <= -1)
		return a;
	else if (cosHalfAngle < 0) {
		b.xyz = -b.xyz;
		b.w = -b.w;
		cosHalfAngle = -cosHalfAngle;
	}

	float blendA, blendB;
	if (cosHalfAngle < 0.999) {
		// do proper slerp for big angles
		float halfAngle = acos(cosHalfAngle);
		float oneOverSinHalfAngle = 1/sin(halfAngle);
		blendA = sin(halfAngle * (1-t)) * oneOverSinHalfAngle;
		blendB = sin(halfAngle * t) * oneOverSinHalfAngle;
	} else {
		// do lerp if angle is really small.
		blendA = 1-t;
		blendB = t;
	}
 	return normalize(make_quatf(blendA*a.xyz + blendB*b.xyz, blendA*a.w + blendB*b.w));
}

inline float max3(float3 x) { return max(max(x[0], x[1]), x[2]); }
inline float max4(float4 x) { return max(max(x[0], x[1]), max(x[2], x[3])); }
inline float min3(float3 x) { return min(min(x[0], x[1]), x[2]); }
inline float min4(float4 x) { return min(min(x[0], x[1]), min(x[2], x[3])); }

#define DECLARE_INTEGER_POW_FNS(T) \
	inline T pow2(T x) { return x*x; } \
	inline T pow3(T x) { return pow2(x)*x; } \
	inline T pow4(T x) { return pow2(x)*pow2(x); } \
	inline T pow5(T x) { return pow4(x)*x; }
DECLARE_INTEGER_POW_FNS(float)

#ifdef __cplusplus
#undef DECLARE_INTEGER_POW_FNS
#define DECLARE_INTEGER_POW_FNS(T) \
	inline T pow2(T x) { return x.cwiseProduct(x); } \
	inline T pow3(T x) { return pow2(x).cwiseProduct(x); } \
	inline T pow4(T x) { return pow2(x).cwiseProduct(pow2(x)); } \
	inline T pow5(T x) { return pow4(x).cwiseProduct(x); }
#endif

DECLARE_INTEGER_POW_FNS(float2)
DECLARE_INTEGER_POW_FNS(float3)
DECLARE_INTEGER_POW_FNS(float4)
#undef DECLARE_INTEGER_POW_FNS

inline float3 hue_to_rgb(float hue) {
	float x = 6*hue;
	return saturate(float3(abs(x-3) - 1, 2 - abs(x-2), 2 - abs(x-4)));
}
inline float3 hsv_to_rgb(float3 hsv) {
	float3 rgb = hue_to_rgb(hsv[0]) - float3(1,1,1);
	return (rgb * hsv[1] + float3(1,1,1)) * hsv[2];
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

inline float3 viridis_quintic(float x) {
	// from https://www.shadertoy.com/view/XtGGzG
	float4 x1 = float4(1, x, x*x, x*x*x); // 1 x x2 x3
	float2 x2 = float2(x1[1], x1[2]) * x1[3]; // x4 x5
	return float3(
		dot(x1, float4( 0.280268003, -0.143510503,   2.225793877, -14.815088879)) + dot(x2, float2( 25.212752309, -11.772589584)),
		dot(x1, float4(-0.002117546,  1.617109353,  -1.909305070,   2.701152864)) + dot(x2, float2(-1.685288385 ,   0.178738871)),
		dot(x1, float4( 0.300805501,  2.614650302, -12.019139090,  28.933559110)) + dot(x2, float2(-33.491294770,  13.762053843)));
}

inline float2 cartesian_to_spherical_uv(float3 xyz) {
	return float2(atan2(xyz[2], xyz[0])*M_1_PI*.5 + .5, acos(xyz[1])*M_1_PI);
}
inline float3 spherical_uv_to_cartesian(float2 uv) {
	uv[0] = uv[0]*2 - 1;
	uv *= M_PI;
	float sinPhi = sin(uv[1]);
	return float3(sinPhi*cos(uv[0]), cos(uv[1]), sinPhi*sin(uv[0]));
}

inline float2 ray_sphere(float3 origin, float3 dir, float3 p, float r) {
	float3 f = origin - p;
	float a = dot(dir, dir);
	float b = dot(f, dir);
	float3 l = a*f - dir*b;
	float det = pow2(a*r) - dot(l,l);
	if (det < 0) return float2(0,0);
	float inv_a = 1/a;
	det = sqrt(det * inv_a) * inv_a;
	return -float2(1,1)*b*inv_a + float2(-det, det);
}
inline float2 ray_box(float3 origin, float3 inv_dir, float3 mn, float3 mx) {
	#ifdef __cplusplus
	float3 t0 = (mn - origin).cwiseProduct(inv_dir);
	float3 t1 = (mx - origin).cwiseProduct(inv_dir);
	#else
	float3 t0 = (mn - origin) * inv_dir;
	float3 t1 = (mx - origin) * inv_dir;
	#endif
	return float2(max3(min(t0, t1)), min3(max(t0, t1)));
}

#endif