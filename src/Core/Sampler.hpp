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
		mDevice.SetObjectName(mSampler, name);
	}
	inline Sampler(Device& device, const string& name, vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) : DeviceResource(device, name) {
		mInfo.magFilter = filter;
		mInfo.minFilter = filter;
		mInfo.addressModeU = addressMode;
		mInfo.addressModeV = addressMode;
		mInfo.addressModeW = addressMode;
		mInfo.anisotropyEnable = maxAnisotropy > 0 ? VK_TRUE : VK_FALSE;
		mInfo.maxAnisotropy = maxAnisotropy;
		mInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
		mInfo.unnormalizedCoordinates = VK_FALSE;
		mInfo.compareEnable = VK_FALSE;
		mInfo.compareOp = vk::CompareOp::eAlways;
		mInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		mInfo.minLod = 0;
		mInfo.maxLod = VK_LOD_CLAMP_NONE;
		mInfo.mipLodBias = 0;
		mSampler = mDevice->createSampler(mInfo);
		mDevice.SetObjectName(mSampler, name);
	}
	inline ~Sampler() {
		mDevice->destroySampler(mSampler);
	}
	inline const vk::Sampler& operator*() const { return mSampler; }
	inline const vk::Sampler* operator->() const { return &mSampler; }

	inline const vk::SamplerCreateInfo& CreateInfo() const { return mInfo; }
};

}