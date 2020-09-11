#pragma once

#include <Core/Device.hpp>

class Asset {
public:
  const std::string mName;

  inline Asset(const fs::path& filename, Device* device, const std::string& name) : mName(name), mDevice(device) {};
  inline virtual ~Asset() {};

	inline ::Device* Device() const { return mDevice; };

protected:
  ::Device* mDevice;
};