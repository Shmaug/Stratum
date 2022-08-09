#pragma once

#include "Scene.hpp"

namespace stm {

struct FlyCamera {
	Node& mNode;

	float mMoveSpeed = 1;
	float mRotateSpeed = 0.002f;
	float2 mRotation = float2::Zero();
	bool mMatchWindowRect = true;

	STRATUM_API FlyCamera(Node& node);
};

}