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

differential3 transfer(float3 D, float3 Ng, float3 P, differential3 dP, differential3 dD) {
  float tmp = 1/dot(D,Ng);
  float t = -dot(P,Ng)*tmp;
  float3 tmpx = dP.dx + dD.dx*t;
  float3 tmpy = dP.dy + dD.dy*t;
  differential3 r;
  r.dx = tmpx - D*dot(tmpx, Ng) * tmp;
  r.dy = tmpy - D*dot(tmpy, Ng) * tmp;
  return r;
}
void differential_dudv(differential3 dPdUV, differential3 dP, float3 Ng, differential du, differential dv) {
  // now we have dPdx/dy from the ray differential transfer, and dPdu/dv
  // from the primitive, we can compute dudx/dy and dvdx/dy. these are
  // mainly used for differentials of arbitrary mesh attributes.

  // find most stable axis to project to 2D
  float xn = abs(Ng.x);
  float yn = abs(Ng.y);
  float zn = abs(Ng.z);

  float3 dPdu = dPdUV.dx;
  float3 dPdv = dPdUV.dy;

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

  float det = (dPdu.x * dPdv.y - dPdv.x * dPdu.y);

  if (det != 0) det = 1/det;

  du.dx = (dP.dx.x * dPdv.y - dP.dx.y * dPdv.x) * det;
  dv.dx = (dP.dx.y * dPdu.x - dP.dx.x * dPdu.y) * det;

  du.dy = (dP.dy.x * dPdv.y - dP.dy.y * dPdv.x) * det;
  dv.dy = (dP.dy.y * dPdu.x - dP.dy.x * dPdu.y) * det;
}


differential3 reflect(float3 D, differential3 dD, float3 N, differential3 dN) {
  differential3 ddn;
  ddn.dx = dot(dD.dx, N) + dot(D, dN.dx);
  ddn.dy = dot(dD.dy, N) + dot(D, dN.dy);
  float dn = dot(D,N);
  differential3 r;
  r.dx = dD.dx - 2*(dn * dN.dx + ddn.dx*N);
  r.dy = dD.dy - 2*(dn * dN.dy + ddn.dy*N);
  return r;
}

differential3 refract(float3 D, differential3 dD, float3 N, differential3 dN, float eta) {
  float dn = dot(D,N);
  differential3 ddn;
  ddn.dx = dot(dD.dx, N) + dot(D, dN.dx);
  ddn.dy = dot(dD.dy, N) + dot(D, dN.dy);
  float eta2 = eta*eta;
  float d1n = -sqrt(1 - eta2*(1 - dn*dn));
  float u = eta*dn - d1n;
  float du_ddn = eta - eta2*dn/d1n;
  differential3 r;
  r.dx = eta*dD.dx - (u*dN.dx + (du_ddn*ddn.dx)*N);
  r.dy = eta*dD.dy - (u*dN.dy + (du_ddn*ddn.dy)*N);
  return r;
}

#endif
#endif