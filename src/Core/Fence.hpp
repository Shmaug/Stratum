#pragma once

#include "Device.hpp"

namespace stm {

class Fence : public DeviceResource {
private:
	vk::Fence mFence;
public:
	inline Fence() = delete;
	inline Fence(Fence&& v) : DeviceResource(v.mDevice, v.name()), mFence(v.mFence) { v.mFence = nullptr; }
	inline Fence(const Fence&) = delete;
	inline Fence(Device& device, const string& name) : DeviceResource(device,name) {
		mFence = mDevice->createFence({});
		mDevice.set_debug_name(mFence, name);
	}
	inline ~Fence() { mDevice->destroyFence(mFence); }
	inline vk::Fence& operator*() { return mFence; }
	inline vk::Fence* operator->() { return &mFence; }
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
	inline Semaphore() = delete;
	inline Semaphore(Semaphore&& v) : DeviceResource(v.mDevice, v.name()), mSemaphore(v.mSemaphore) { v.mSemaphore = nullptr; }
	inline Semaphore(const Semaphore&) = delete;
	inline Semaphore(Device& device, const string& name) : DeviceResource(device, name) {
		mSemaphore = mDevice->createSemaphore({});
		mDevice.set_debug_name(mSemaphore, name);
	}
	inline ~Semaphore() { mDevice->destroySemaphore(mSemaphore); }
	inline vk::Semaphore& operator*() { return mSemaphore; }
	inline vk::Semaphore* operator->() { return &mSemaphore; }
	inline const vk::Semaphore& operator*() const { return mSemaphore; }
	inline const vk::Semaphore* operator->() const { return &mSemaphore; }
};

class Event : public DeviceResource {
private:
	vk::Event mEvent;
public:
	inline Event() = delete;
	inline Event(Event&& v) : DeviceResource(v.mDevice, v.name()), mEvent(v.mEvent) { v.mEvent = nullptr; }
	inline Event(const Event&) = delete;
	inline Event(Device& device, const string& name) : DeviceResource(device,name) {
		mEvent = mDevice->createEvent({});
		mDevice.set_debug_name(mEvent, name);
	}
	inline ~Event() { mDevice->destroyEvent(mEvent); }
	inline vk::Event& operator*() { return mEvent; }
	inline vk::Event* operator->() { return &mEvent; }
	inline const vk::Event& operator*() const { return mEvent; }
	inline const vk::Event* operator->() const { return &mEvent; }
	inline bool signalled() { return mDevice->getEventStatus(mEvent) == vk::Result::eEventSet; }
};

}