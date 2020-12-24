#pragma once

#include "../Stratum.hpp"

namespace stm {

// Generalization of a mouse+keyboard state, or a pose from a tracked device, etc.
class InputState {
public:
	InputState() = default;
	InputState(const InputState&) = default;
	InputState(InputState&&) = default;
	virtual ~InputState() = default;

	inline InputState(const string& id) : mId(id) {};
	inline string Id() const { return mId; }

private:
	string mId; // should be globally unique
};

}