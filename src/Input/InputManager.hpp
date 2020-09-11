#pragma once

#include <Input/InputDevice.hpp>

class InputManager {
public:
	STRATUM_API void RegisterInputDevice(InputDevice* device);
	STRATUM_API void UnregisterInputDevice(InputDevice* device);

	inline const std::vector<InputDevice*>& InputDevices() const { return mInputDevices; }

	template<class T>
	inline T* GetFirst() {
		for (InputDevice* p : mInputDevices)
			if (T* t = dynamic_cast<T*>(p))
				return t;
		return nullptr;
	}

	template<class T>
	inline void GetAll(std::vector<T*>& devices) {
		for (InputDevice* p : mInputDevices)
			if (T* t = dynamic_cast<T*>(p))
				devices.push_back(t);
	}

private:
	friend class Stratum;
	std::vector<InputDevice*> mInputDevices;
};