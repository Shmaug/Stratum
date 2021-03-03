#pragma once

#include "../Core/InputState.hpp"
#include "Scene.hpp"

namespace stm {

class PhysicsSim : public SceneNode::Component {
public:
  inline PhysicsSim(SceneNode& node, const string& name) : SceneNode::Component(node, name) {
		mStartTime = mLastFrame = mClock.now();
	}

  STRATUM_API void Update(CommandBuffer& commandBuffer);

	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float PhysicsTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }

	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }

private:
	float mPhysicsTimeLimitPerFrame = 0.1f;
	float mFixedAccumulator = 0;
	float mFixedTimeStep = 1.f/60.f;

	float mTotalTime = 0;
	float mDeltaTime = 0;

	chrono::high_resolution_clock mClock;
	chrono::high_resolution_clock::time_point mStartTime;
	chrono::high_resolution_clock::time_point mLastFrame;
};

}
