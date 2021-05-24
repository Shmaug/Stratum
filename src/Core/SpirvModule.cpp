#include "SpirvModule.hpp"

#include <json.hpp>

using namespace stm;

SpirvModule::SpirvModule(Device& device, const fs::path& spvasm) : DeviceResource(device, spvasm.stem().string()) {
  auto spirv = read_file<vector<uint32_t>>(spvasm);
  mShaderModule = device->createShaderModule(vk::ShaderModuleCreateInfo({}, spirv));

  std::ifstream s(spvasm.replace_extension("json"));
  if (s.is_open()) {
    nlohmann::json j;
    s >> j;

  }
}