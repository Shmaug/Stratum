#pragma once

#include <Core/Device.hpp>

namespace stm {

class Asset {
public:
  const std::string mName;
  stm::Device* const mDevice;

  inline Asset(const fs::path& filename, Device* device, const std::string& name) : mName(name), mDevice(device) {};
  inline virtual ~Asset() {};
};

}