#include <microfacet.h>

struct DisneyBSDF {
    Spectrum base_color;
    Real specular_transmission;
    Real metallic;
    Real subsurface;
    Real specular;
    Real roughness;
    Real specular_tint;
    Real anisotropic;
    Real sheen;
    Real sheen_tint;
    Real clearcoat;
    Real clearcoat_gloss;
    Real eta;
};

Spectrum eval() const {
    const Real one_minus_metallic = 1 - eval(bsdf.metallic, vertex.uv, vertex.uv_screen_size, texture_pool);
    const Real specular_transmission = eval(bsdf.specular_transmission, vertex.uv, vertex.uv_screen_size, texture_pool);

    Vector3 f = make_zero_spectrum();

    const Real w_glass = one_minus_metallic * specular_transmission;
    if (w_glass > 0) f += w_glass * this->operator()(DisneyGlass{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic, bsdf.eta });

    if (dot(vertex.geometry_normal, dir_in) > 0 && dot(vertex.geometry_normal, dir_out) > 0) {
        const Real w_diffuse   = (1 - specular_transmission) * one_minus_metallic;
        const Real w_sheen     = one_minus_metallic;
        const Real w_metal     =  1 - specular_transmission * one_minus_metallic;
        const Real w_clearcoat = 0.25 * eval(bsdf.clearcoat, vertex.uv, vertex.uv_screen_size, texture_pool);

        if (w_diffuse > 0) f += w_diffuse * this->operator()(DisneyDiffuse{ bsdf.base_color, bsdf.roughness, bsdf.subsurface });
        if (w_sheen > 0)   f += w_sheen   * this->operator()(DisneySheen{ bsdf.base_color, bsdf.sheen_tint });
        if (w_metal > 0) {
            // fake dialectric specular reflection
            const Real specular       = eval(bsdf.specular, vertex.uv, vertex.uv_screen_size, texture_pool);
            const Real specular_tint  = eval(bsdf.specular_tint, vertex.uv, vertex.uv_screen_size, texture_pool);
            const Spectrum base_color = eval(bsdf.base_color, vertex.uv, vertex.uv_screen_size, texture_pool);
            const Spectrum Ctint = luminance(base_color) > 0 ? base_color/luminance(base_color) : make_const_spectrum(1);
            const Spectrum Ks = (1 - specular_tint) + specular_tint*Ctint;
            const Spectrum C0 = specular*R0(bsdf.eta)*one_minus_metallic*Ks + (1 - one_minus_metallic)*base_color;
            f += w_metal * this->operator()(DisneyMetal{ make_constant_spectrum_texture(C0), bsdf.roughness, bsdf.anisotropic });
        }
        if (w_clearcoat > 0) f += w_clearcoat * this->operator()(DisneyClearcoat{ bsdf.clearcoat_gloss });
    }
    return f;
}

Real pdf_sample_bsdf_op::operator()(const DisneyBSDF &bsdf) const {
    Real pdf = 0;

    const Real one_minus_metallic = 1 - eval(bsdf.metallic, vertex.uv, vertex.uv_screen_size, texture_pool);
    const Real specular_transmission = eval(bsdf.specular_transmission, vertex.uv, vertex.uv_screen_size, texture_pool);

    const Real w_glass = one_minus_metallic * specular_transmission;
    if (w_glass > 0) pdf += w_glass * this->operator()(DisneyGlass{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic, bsdf.eta });
    Real w_sum = w_glass;

    if (dot(vertex.geometry_normal, dir_in) > 0) {
        const Real w_diffuse   = (1 - specular_transmission) * one_minus_metallic;
        const Real w_metal     =  1 - specular_transmission * one_minus_metallic;
        const Real w_clearcoat = 0.25 * eval(bsdf.clearcoat, vertex.uv, vertex.uv_screen_size, texture_pool);

        if (w_diffuse > 0) pdf += w_diffuse * this->operator()(DisneyDiffuse{ bsdf.base_color, bsdf.roughness, bsdf.subsurface });
        if (w_metal > 0) pdf += w_metal * this->operator()(DisneyMetal{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic });
        if (w_clearcoat > 0) pdf += w_clearcoat * this->operator()(DisneyClearcoat{ bsdf.clearcoat_gloss });
        w_sum += w_diffuse + w_metal + w_clearcoat;
    }
    return w_sum == 0 ? pdf : pdf / w_sum;
}

std::optional<BSDFSampleRecord> sample_bsdf_op::operator()(const DisneyBSDF &bsdf) const {
    if (dot(vertex.geometry_normal, dir_in) > 0) {
        const Real one_minus_metallic = 1 - eval(bsdf.metallic, vertex.uv, vertex.uv_screen_size, texture_pool);
        const Real specular_transmission = eval(bsdf.specular_transmission, vertex.uv, vertex.uv_screen_size, texture_pool);

        const Real w_glass     = one_minus_metallic * specular_transmission;
        const Real w_diffuse   = (1 - specular_transmission) * one_minus_metallic;
        const Real w_metal     =  1 - specular_transmission * one_minus_metallic;
        const Real w_clearcoat = 0.25 * eval(bsdf.clearcoat, vertex.uv, vertex.uv_screen_size, texture_pool);

        Real cdf[4];
        cdf[0] = w_glass;
        cdf[1] = cdf[0] + w_diffuse;
        cdf[2] = cdf[1] + w_metal;
        cdf[3] = cdf[2] + w_clearcoat;
        const Real r = rnd_param_w * cdf[3];

        if (rnd_param_w <= cdf[0]) {
            const sample_bsdf_op op{ dir_in, vertex, texture_pool, rnd_param_uv, rnd_param_w / w_glass, dir };
            return op(DisneyGlass{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic, bsdf.eta });
        } else if (r <= cdf[1]) {
            const sample_bsdf_op op{ dir_in, vertex, texture_pool, rnd_param_uv, (r -cdf[0]) / w_diffuse, dir };
            return op(DisneyDiffuse{ bsdf.base_color, bsdf.roughness, bsdf.subsurface });
        } else if (r <= cdf[2]) {
            const sample_bsdf_op op{ dir_in, vertex, texture_pool, rnd_param_uv, (r - cdf[1]) / w_metal, dir };
            return op(DisneyMetal{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic });
        } else {
            const sample_bsdf_op op{ dir_in, vertex, texture_pool, rnd_param_uv, (r - cdf[2]) / (cdf[3] - w_clearcoat), dir };
            return op(DisneyClearcoat{ bsdf.clearcoat_gloss });
        }
    } else
        return this->operator()(DisneyGlass{ bsdf.base_color, bsdf.roughness, bsdf.anisotropic, bsdf.eta });
}