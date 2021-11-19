#ifndef RAY_DIFFERENTIAL_H
#define RAY_DIFFERENTIAL_H

#include "../math.hlsli"

struct differential {
  float dx;
  float dy;
};
struct differential2 {
  float2 dx;
  float2 dy;
};
struct differential3 {
  float3 dx;
  float3 dy;
};

#ifndef __cplusplus

differential3 transfer(float3 Ng, float3 P, float3 D, float t, differential3 dP, differential3 dD) {
  float3 tmpx = dP.dx + dD.dx*t;
  float3 tmpy = dP.dy + dD.dy*t;
  differential3 r;
  float3 tmp = D/dot(D,Ng);
  r.dx = tmpx - tmp*dot(tmpx, Ng);
  r.dy = tmpy - tmp*dot(tmpy, Ng);
  return r;
}
void differential_dudv(float3 dPdu, float3 dPdv, differential3 dP, float3 Ng, out differential du, out differential dv) {
  // now we have dPdx/dy from the ray differential transfer, and dPdu/dv
  // from the primitive, we can compute dudx/dy and dvdx/dy. these are
  // mainly used for differentials of arbitrary mesh attributes.

  // find most stable axis to project to 2D
  float xn = abs(Ng.x);
  float yn = abs(Ng.y);
  float zn = abs(Ng.z);

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

  du.dx = (dP.dx.x*dPdv.y - dP.dx.y*dPdv.x) * det;
  du.dy = (dP.dy.x*dPdv.y - dP.dy.y*dPdv.x) * det;

  dv.dx = (dP.dx.y*dPdu.x - dP.dx.x*dPdu.y) * det;
  dv.dy = (dP.dy.y*dPdu.x - dP.dy.x*dPdu.y) * det;
}

#endif
#endif