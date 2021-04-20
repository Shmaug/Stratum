#pragma once

#include "Device.hpp"

namespace stm {

class Fence : public DeviceResource {
private:
	vk::Fence mFence;
public:
	inline Fence(Device& device, const string& name) : DeviceResource(device,name) {
		mFence = mDevice->createFence({});
		mDevice.SetObjectName(mFence, name);
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline const vk::Fence& operator*() const { return mFence; }
	inline const vk::Fence* operator->() const { return &mFence; }
	inline vk::Result status() { return mDevice->getFenceStatus(mFence); }
	inline vk::Result wait(uint64_t timeout = numeric_limits<uint64_t>::max()) {
		return mDevice->waitForFences({ mFence }, true, timeout);
	}
};

class Semaphore : public DeviceResource {
private:
	vk::Semaphore mSemaphore;
public:
	inline Semaphore(Device& device, const string& name) : DeviceResource(device, name) {
		mSemaphore = mDevice->createSemaphore({});
		mDevice.SetObjectName(mSemaphore, name);
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline const vk::Semaphore& operator*() { return mSemaphore; }
	inline const vk::Semaphore* operator->() { return &mSemaphore; }
};

}