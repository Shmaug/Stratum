#pragma once

#include "../Stratum.hpp"

namespace stm {

// Generalization of a mouse+keyboard state, or a pose from a tracked device, etc.
class InputState {
protected:
	string mName; // should be globally unique
public:
	InputState() = default;
	InputState(const InputState&) = default;
	InputState(InputState&&) = default;
	virtual ~InputState() = default;

	inline InputState(const string& id) : mName(id) {};
	inline string Name() const { return mName; }
};

}