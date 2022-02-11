#ifndef RAY_DIFFERENTIAL_H
#define RAY_DIFFERENTIAL_H

#include "common.hlsli"

struct differential {
  min16float dx;
  min16float dy;
  inline float2 xy() CONST_CPP { return float2(dx, dy); }
};
struct differential2 {
  min16float2 dx;
  min16float2 dy;
};
struct differential3 {
  min16float3 dx;
  min16float3 dy;
};

struct RayDifferential {
  float3 origin;
  float3 direction;
  float t_min;
  float t_max;
  differential3 dP;
  differential3 dD;
};

inline differential3 transfer_dP(const float3 Ng, const float3 P, const float3 D, const float t, const differential3 dP, const differential3 dD) {
  const float3 tmpx = dP.dx + dD.dx*t;
  const float3 tmpy = dP.dy + dD.dy*t;
  const float3 tmp = D/dot(D,Ng);
  const differential3 r = {
    tmpx - tmp*dot(tmpx, Ng),
    tmpy - tmp*dot(tmpy, Ng) };
  return r;
}
inline void differential_dudv(float3 dPdu, float3 dPdv, differential3 dP, const float3 Ng, ARG_OUT(differential) du, ARG_OUT(differential) dv) {
  // now we have dPdx/dy from the ray differential transfer, and dPdu/dv
  // from the primitive, we can compute dudx/dy and dvdx/dy. these are
  // mainly used for differentials of arbitrary mesh attributes.

  // find most stable axis to project to 2D
  const float xn = abs(Ng.x);
  const float yn = abs(Ng.y);
  const float zn = abs(Ng.z);

  if (zn < xn || zn < yn) {
    if (yn < xn || yn < zn) {
      dPdu.x = dPdu.y;
      dPdv.x = dPdv.y;
      dP.dx.x = dP.dx.y;
      dP.dy.x = dP.dy.y;
    }

    dPdu.y = dPdu.z;
    dPdv.y = dPdv.z;
    dP.dx.y = dP.dx.z;
    dP.dy.y = dP.dy.z;
  }

  // using Cramer's rule, we solve for dudx and dvdx in a 2x2 linear system,
  // and the same for dudy and dvdy. the denominator is the same for both
  // solutions, so we compute it only once.
  // dP.dx = dPdu * dudx + dPdv * dvdx;
  // dP.dy = dPdu * dudy + dPdv * dvdy;

  float det = dPdu.x*dPdv.y - dPdv.x*dPdu.y;
  if (det != 0) det = 1/det;

  du.dx = (min16float)((dP.dx.x*dPdv.y - dP.dx.y*dPdv.x) * det);
  du.dy = (min16float)((dP.dy.x*dPdv.y - dP.dy.y*dPdv.x) * det);

  dv.dx = (min16float)((dP.dx.y*dPdu.x - dP.dx.x*dPdu.y) * det);
  dv.dy = (min16float)((dP.dy.y*dPdu.x - dP.dy.x*dPdu.y) * det);
}
inline differential3 reflect(const differential3 i, const float3 n) {
  differential3 r;
  r.dx = reflect(i.dx, (min16float3)n);
  r.dy = reflect(i.dy, (min16float3)n);
  return r;
}
inline differential3 refract(const differential3 i, const float3 n, const float eta) {
  differential3 r;
  r.dx = refract(i.dx, (min16float3)n, (min16float)eta);
  r.dy = refract(i.dy, (min16float3)n, (min16float)eta);
  return r;
}

#endif