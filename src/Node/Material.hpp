#pragma once

namespace stm {

#pragma pack(push)
#pragma pack(1)
#include <Shaders/scene.h>
#include <Shaders/environment.h>
#include <Shaders/materials/disney_data.h>
#pragma pack(pop)

struct Material {
    ImageValue4 data[DISNEY_DATA_N];
	Image::View alpha_mask;
	Buffer::View<uint32_t> min_alpha;
	Image::View bump_image;
	float bump_strength;

	bool alpha_test() { return (!min_alpha || min_alpha.buffer()->in_use()) ? false : min_alpha[0] < 0xFFFFFFFF/2; }

    inline void store(ByteAppendBuffer& bytes, MaterialResources& resources) const {
		for (int i = 0; i < DISNEY_DATA_N; i++)
        	data[i].store(bytes, resources);
		bytes.Append(resources.get_index(alpha_mask));
		bytes.Append(resources.get_index(bump_image));
		bytes.Appendf(bump_strength);
    }
    inline void inspector_gui() {
		ImGui::ColorEdit3("Base Color"        , data[0].value.data());
		ImGui::PushItemWidth(80);
		ImGui::DragFloat("Emission"           , &data[0].value[3]);
		ImGui::DragFloat("Metallic"           , &data[1].value[0], 0.1, 0, 1);
		ImGui::DragFloat("Roughness"          , &data[1].value[1], 0.1, 0, 1);
		ImGui::DragFloat("Anisotropic"        , &data[1].value[2], 0.1, 0, 1);
		ImGui::DragFloat("Subsurface"         , &data[1].value[3], 0.1, 0, 1);
		ImGui::DragFloat("Clearcoat"          , &data[2].value[0], 0.1, 0, 1);
		ImGui::DragFloat("Clearcoat Gloss"    , &data[2].value[1], 0.1, 0, 1);
		ImGui::DragFloat("Transmission"       , &data[2].value[2], 0.1, 0, 1);
		ImGui::DragFloat("Index of Refraction", &data[2].value[3], 0.1, 0, 2);
		if (bump_image) ImGui::DragFloat("Bump Strength", &bump_strength, 0.1, 0, 10);
		ImGui::PopItemWidth();

		const float w = ImGui::CalcItemWidth() - 4;
		for (uint i = 0; i < DISNEY_DATA_N; i++)
			if (data[i].image) {
				ImGui::Text(data[i].image.image()->name().c_str());
				ImGui::Image(&data[i].image, ImVec2(w, w * data[i].image.extent().height / (float)data[i].image.extent().width));
			}
		if (alpha_mask) {
			ImGui::Text(alpha_mask.image()->name().c_str());
			ImGui::Image(&alpha_mask, ImVec2(w, w * alpha_mask.extent().height / (float)alpha_mask.extent().width));
		}
		if (bump_image) {
			ImGui::Text(bump_image.image()->name().c_str());
			ImGui::Image(&bump_image, ImVec2(w, w * bump_image.extent().height / (float)bump_image.extent().width));
		}
    }
};

struct Medium {
	float3 density_scale;
	float anisotropy;
	float3 albedo_scale;
	float attenuation_unit;
	component_ptr<nanovdb::GridHandle<nanovdb::HostBuffer>> density_grid, albedo_grid;
	Buffer::View<byte> density_buffer, albedo_buffer;

	inline void store(ByteAppendBuffer& bytes, MaterialResources& pool) const {
		bytes.AppendN(density_scale);
		bytes.Appendf(anisotropy);
		bytes.AppendN(albedo_scale);
		bytes.Appendf(attenuation_unit);
		bytes.Append(pool.get_index(density_buffer));
		bytes.Append(pool.get_index(albedo_buffer));
	}
	inline void inspector_gui() {
		ImGui::ColorEdit3("Density", density_scale.data(), ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Albedo", albedo_scale.data(), ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Anisotropy", &anisotropy, -.999f, .999f);
		ImGui::SliderFloat("Attenuation Unit", &attenuation_unit, 0, 1);
	}
};

}