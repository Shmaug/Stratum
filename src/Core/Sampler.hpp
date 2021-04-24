#pragma once

#include "Device.hpp"

namespace stm {

class Sampler : public DeviceResource {
private:
	vk::Sampler mSampler;
	vk::SamplerCreateInfo mInfo;

public:
	inline Sampler(Device& device, const string& name, const vk::SamplerCreateInfo& samplerInfo) : DeviceResource(device, name), mInfo(samplerInfo) {
		mSampler = mDevice->createSampler(mInfo);
		mDevice.set_debug_name(mSampler, name);
	}
	inline ~Sampler() {
		mDevice->destroySampler(mSampler);
	}
	inline const vk::Sampler& operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }

	inline const vk::SamplerCreateInfo& create_info() const { return mInfo; }
};

}