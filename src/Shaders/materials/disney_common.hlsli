#include "../microfacet.h"

Real Dm(const Real alpha_x, const Real alpha_y, const Vector3 h_l) {
    const Real alpha_x2 = alpha_x * alpha_x;
    const Real alpha_y2 = alpha_y * alpha_y;
    const Vector3 h_l2 = h_l * h_l;
    const Real hh = h_l2.x/alpha_x2 + h_l2.y/alpha_y2 + h_l2.z;
    return 1 / (M_PI * alpha_x * alpha_y * hh*hh);
}
Real G1(const Real alpha_x, const Real alpha_y, const Vector3 w_l) {
    const Real alpha_x2 = alpha_x * alpha_x;
    const Real alpha_y2 = alpha_y * alpha_y;
    const Vector3 w_l2 = w_l * w_l;
    const Real lambda = (sqrt(1 + (w_l2.x*alpha_x2 + w_l2.y*alpha_y2) / w_l2.z) - 1) / 2;
    return 1 / (1 + lambda);
}
Real R0(const Real eta) {
    const Real num = eta - 1;
    const Real denom = eta + 1;
    return (num*num) / (denom*denom);
}

Real Dc(const Real alpha_g, const Real h_lz) {
    const Real alpha_g2 = alpha_g * alpha_g;
    return (alpha_g2 - 1) / (M_PI * log(alpha_g2)*(1 + (alpha_g2 - 1)*h_lz*h_lz));
}
Real Gc(const Vector3 w_l) {
    const Real wx = w_l.x*0.25;
    const Real wy = w_l.y*0.25;
    const Real lambda = (sqrt(1 + (wx*wx + wy*wy)/(w_l.z*w_l.z)) - 1) / 2;
    return 1 / (1 + lambda);
}

struct DisneyMaterialData {
	float4 data[2];
	Spectrum base_color()  { return data[0].rgb; }
	Real emission()        { return data[0].a; }
	Real metallic()        { return data[1].r; }
	Real roughness()       { return data[1].g; }
	Real anisotropic()     { return data[1].b; }
	Real subsurface()      { return data[1].a; }
	Real clearcoat()       { return data[2].r; }
	Real clearcoat_gloss() { return data[2].g; }
	Real transmission()    { return data[2].b; }
	Real eta()             { return data[2].a; }
	Real sheen_tint()      { return 0; }

	Real alpha()           { return roughness()*roughness(); }
};