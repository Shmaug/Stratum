#ifndef MATERIAL_H
#define MATERIAL_H

#include "environment.h"
#include "materials/disney_data.h"

struct Material {
    ImageValue4 data[DISNEY_DATA_N];

    inline void store(ByteAppendBuffer& bytes, MaterialResources& resources) const {
		for (int i = 0; i < DISNEY_DATA_N; i++)
        	data[i].store(bytes, resources);
    }
    inline void inspector_gui() {
		ImGui::ColorEdit3("Base Color"        , data[0].value.data());
		ImGui::DragFloat("Emission"           , &data[0].value[3]);
		ImGui::DragFloat("Metallic"           , &data[1].value[0], 0.1, 0, 1);
		ImGui::DragFloat("Roughness"          , &data[1].value[1], 0.1, 0, 1);
		ImGui::DragFloat("Anisotropic"        , &data[1].value[2], 0.1, 0, 1);
		ImGui::DragFloat("Subsurface"         , &data[1].value[3], 0.1, 0, 1);
		ImGui::DragFloat("Clearcoat"          , &data[2].value[0], 0.1, 0, 1);
		ImGui::DragFloat("Clearcoat Gloss"    , &data[2].value[1], 0.1, 0, 1);
		ImGui::DragFloat("Transmission"       , &data[2].value[2], 0.1, 0, 1);
		ImGui::DragFloat("Index of Refraction", &data[2].value[3], 0.1, 0, 1);
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

#endif