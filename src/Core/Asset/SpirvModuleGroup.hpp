#pragma once

#include "../Device.hpp"
#include "../SpirvModule.hpp"

namespace stm {

class SpirvModuleGroup : public unordered_map<string, SpirvModule>, public Asset {
public:
	using container_t = unordered_map<string, SpirvModule>;

	SpirvModuleGroup() = default;
	inline SpirvModuleGroup(Device& device, const container_t& modules) : container_t(modules) {
		for (auto& [id, m] : *this)
			if (m.mDevice && !m.mShaderModule)
				m.mShaderModule = m.mDevice.createShaderModule(vk::ShaderModuleCreateInfo({}, m.mSpirv));
	}
	inline SpirvModuleGroup(Device& device, const fs::path& filename) {
		binary_stream stream(filename);
		stream >> static_cast<container_t&>(*this); // binary_stream<vector<SpirvModule>>::operator>>(stream, *this);
		for (auto& [id, m] : *this) {
			m.mDevice = *device;
			m.mShaderModule = device->createShaderModule(vk::ShaderModuleCreateInfo({}, m.mSpirv));
		}
	}
};

inline binary_stream& operator<<(binary_stream& lhs, const SpirvModuleGroup& rhs) { return lhs << (SpirvModuleGroup::container_t&)rhs; }
inline binary_stream& operator>>(binary_stream& lhs, SpirvModuleGroup& rhs) { return lhs >> (SpirvModuleGroup::container_t&)rhs; }

}