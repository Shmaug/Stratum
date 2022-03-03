#ifndef VOLUME_H
#define VOLUME_H

#include "../scene.hlsli"

struct Volume {
    ImageValue3 density;
    
#ifdef __HLSL_VERSION
    template<typename Real, bool TransportToLight>
    inline BSDFEvalRecord<Real> eval(const vector<Real,3> dir_in, const vector<Real,3> dir_out, const PathVertexGeometry vertex) {
        return vertex.eval(volume);
    }

    template<typename Real, bool TransportToLight>
    inline BSDFSampleRecord<Real> sample(const vector<Real,3> rnd, const vector<Real,3> dir_in, const PathVertexGeometry vertex) {
        if (dot(dir_in, vertex.shading_normal) < 0) {
            BSDFSampleRecord<Real> r;
            r.eval.f = 0;
            r.eval.pdfW = 0;
            return r;
        }

        const vector<Real,3> local_dir_out = sample_cos_hemisphere(rnd.x, rnd.y);
        BSDFSampleRecord<Real> r;
        r.dir_out = vertex.shading_frame().to_world(local_dir_out);
        r.eta = 0;
        r.roughness = 1;
        r.eval.f = local_dir_out.z * vertex.eval(reflectance) / M_PI;
        r.eval.pdfW = cosine_hemisphere_pdfW(local_dir_out.z);
        return r;
    }

    template<typename Real> inline vector<Real,3> eval_albedo  (const PathVertexGeometry vertex) { return vertex.eval(reflectance); }
    template<typename Real> inline vector<Real,3> eval_emission(const PathVertexGeometry vertex) { return 0; }

    inline void load(ByteAddressBuffer bytes, inout uint address) {
        reflectance.load(bytes, address);
    }

#endif // __HLSL_VERSION

#ifdef __cplusplus
    Image::View image;

    inline void store(ByteAppendBuffer& bytes, ImagePool& images) const {
        bytes.store(image.get_index(image)
    }
    inline void inspector_gui() {
        static int slice = 0;
        ImGui::IntSlider("Slice", slice);
        density;
        image_value_field("Density", density);
    }

#endif
};

#endif