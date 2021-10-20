#include "../math.hlsli"

struct differential {
  float dx;
  float dy;
};
struct differential3 {
  float3 dx;
  float3 dy;
};

differential3 differential_transfer(differential3 dP, float3 D, differential3 dD, float3 Ng, float t) {
  /* ray differential transfer through homogeneous medium, to
   * compute dPdx/dy at a shading point from the incoming ray */

  float3 tmp = D / dot(D, Ng);
  float3 tmpx = dP.dx + t * dD.dx;
  float3 tmpy = dP.dy + t * dD.dy;
  
  differential3 r;
  r.dx = tmpx - dot(tmpx, Ng) * tmp;
  r.dy = tmpy - dot(tmpy, Ng) * tmp;
  return r;
}

differential3 differential_incoming(differential3 dD) {
  /* compute dIdx/dy at a shading point, we just need to negate the
   * differential of the ray direction */
  
  differential3 r;
  r.dx = -dD.dx;
  r.dy = -dD.dy;
  return r;
}

void differential_dudv(out differential du, out differential dv, float3 dPdu, float3 dPdv, differential3 dP, float3 Ng) {
  /* now we have dPdx/dy from the ray differential transfer, and dPdu/dv
   * from the primitive, we can compute dudx/dy and dvdx/dy. these are
   * mainly used for differentials of arbitrary mesh attributes. */

  /* find most stable axis to project to 2D */
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

  /* using Cramer's rule, we solve for dudx and dvdx in a 2x2 linear system,
   * and the same for dudy and dvdy. the denominator is the same for both
   * solutions, so we compute it only once.
   *
   * dP.dx = dPdu * dudx + dPdv * dvdx;
   * dP.dy = dPdu * dudy + dPdv * dvdy; */

  float det = (dPdu.x * dPdv.y - dPdv.x * dPdu.y);

  if (det != 0.0f)
    det = 1.0f / det;

  du.dx = (dP.dx.x * dPdv.y - dP.dx.y * dPdv.x) * det;
  dv.dx = (dP.dx.y * dPdu.x - dP.dx.x * dPdu.y) * det;

  du.dy = (dP.dy.x * dPdv.y - dP.dy.y * dPdv.x) * det;
  dv.dy = (dP.dy.y * dPdu.x - dP.dy.x * dPdu.y) * det;
}

differential differential_zero() {
  differential d;
  d.dx = 0;
  d.dy = 0;
  return d;
}
differential3 differential3_zero() {
  differential3 d;
  d.dx = 0;
  d.dy = 0;
  return d;
}