#ifndef HET_VOLUME_H
#define HET_VOLUME_H

#ifdef __cplusplus
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

#ifdef __HLSL__
#include "medium.hlsli"
#endif

#endif