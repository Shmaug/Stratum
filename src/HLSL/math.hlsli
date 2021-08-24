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

#define QUATF_I make_quatf(0,0,0,1)

inline quatf angle_axis(float angle, float3 axis) {
	return make_quatf(axis*sin(angle/2), cos(angle/2));
}
inline quatf q_look_at(float3 forward, float3 up) {
	float3 right = normalize(cross(forward, up));
	up = normalize(cross(forward, right));
	float diag = right[0] + up[1] + forward[2];
	quatf q;
	if (diag > 0) {
		float num = sqrt(diag + 1);
		q.w = num/2;
		num = 0.5/num;
		q.xyz[0] = (up[2] - forward[1]) * num;
		q.xyz[1] = (forward[0] - right[2]) * num;
		q.xyz[2] = (right[1] - up[0]) * num;
	} else if ((right[0] >= up[1]) && (right[0] >= forward[2])) {
		float num = sqrt(1 + right[0] - up[1] - forward[2]);
		q.xyz[0] = num/2;
		num = 0.5 / num;
		q.xyz[1] = (right[1] + up[0]) * num;
		q.xyz[2] = (right[2] + forward[0]) * num;
		q.w = (up[2] - forward[1]) * num;
	} else if (up[1] > forward[2]) {
		float num = sqrt(1 + up[1] - right[0] - forward[2]);
		q.xyz[1] = num/2;
		num = 0.5 / num;
		q.xyz[0] = (up[0] + right[1]) * num;
		q.xyz[2] = (forward[1] + up[2]) * num;
		q.w = (forward[0] - right[2]) * num;
	} else {
		float num = sqrt(1 + forward[2] - right[0] - up[1]);
		q.xyz[2] = num/2;
		num = 0.5 / num;
		q.xyz[0] = (forward[0] + right[2]) * num;
		q.xyz[1] = (forward[1] + up[2]) * num;
		q.w = (right[1] - up[0]) * num;
	}
	return q;
}

inline quatf normalize(quatf q) {
	float nrm = 1 / sqrt(q.xyz[0]*q.xyz[0] + q.xyz[1]*q.xyz[1] + q.xyz[2]*q.xyz[2] + q.w*q.w);
	return make_quatf(q.xyz*nrm, q.w*nrm);
}
inline quatf conj(quatf q) { return make_quatf(-q.xyz, q.w); }
inline quatf qmul(quatf q1, quatf q2) {
	quatf r;
  r.xyz[0] = (q1.w * q2.xyz[0]) + (q1.xyz[0] * q2.w) + (q1.xyz[1] * q2.xyz[2]) - (q1.xyz[2] * q2.xyz[1]);
  r.xyz[1] = (q1.w * q2.xyz[1]) - (q1.xyz[0] * q2.xyz[2]) + (q1.xyz[1] * q2.w) + (q1.xyz[2] * q2.xyz[0]);
  r.xyz[2] = (q1.w * q2.xyz[2]) + (q1.xyz[0] * q2.xyz[1]) - (q1.xyz[1] * q2.xyz[0]) + (q1.xyz[2] * q2.w);
  r.w = (q1.w * q2.w) - (q1.xyz[0] * q2.xyz[0]) - (q1.xyz[1] * q2.xyz[1]) - (q1.xyz[2] * q2.xyz[2]);
	return r;
}
inline float3 rotate_vector(quatf q, float3 v) {
	float3 tmp = cross(q.xyz, v) + v*q.w;
  return v + 2*cross(q.xyz, tmp);
}
inline quatf slerp(quatf a, quatf b, float t) {
	float cosHalfAngle = a.w * b.w + dot(a.xyz, b.xyz);

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

#ifdef __cplusplus
inline float3 homogeneous(float2 v) { return v.matrix().homogeneous(); }
inline float4 homogeneous(float3 v) { return v.matrix().homogeneous(); }
inline float2 hnormalized(float3 v) { return v.matrix().hnormalized(); }
inline float3 hnormalized(float4 v) { return v.matrix().hnormalized(); }
#else
inline float3 homogeneous(float2 v) { return float3(v.xy,1); }
inline float4 homogeneous(float3 v) { return float4(v.xyz,1); }
inline float2 hnormalized(float3 v) { return v.xy/v[2]; }
inline float3 hnormalized(float4 v) { return v.xyz/v.w; }
#endif

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
	if (det < 0) return float2(0,0);
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