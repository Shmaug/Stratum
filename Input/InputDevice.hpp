#pragma once

#include <Util/Util.hpp>

class InputDevice;

// Represents a device capable of "pointing" into the world
// i.e. a mouse or Vive controller
// an InputDevice might have multiple pointers (i.e. multiple fingers)
// Each input device has 5 axis, where Axis[0] is the 'primary', Axis[1] is 'secondary' etc..
class InputPointer {
public:
	InputDevice* mDevice;
	Ray mWorldRay;
	Ray mLastWorldRay;
	float mGuiHitT;
	float mLastGuiHitT;
	float mAxis[5];
	float mLastAxis[5];
};

class InputDevice {
public:
	inline virtual uint32_t PointerCount() { return 0; };
	// Get info about a pointer
	inline virtual const InputPointer* GetPointer(uint32_t index) { return nullptr; };

	inline virtual void NextFrame() {}
};