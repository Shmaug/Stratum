#pragma once

#include "../Device.hpp"

namespace stm {

class Asset {
public:
  inline Asset(Device& device, const fs::path& filename) {};
  inline virtual ~Asset() {};
};

}