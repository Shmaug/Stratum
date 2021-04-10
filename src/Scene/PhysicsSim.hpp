#pragma once

#include "Scene.hpp"

namespace stm {

class PhysicsSim : public Scene::Node {
private:
	float mPhysicsTimeLimitPerFrame = 0.1f;
	float mFixedAccumulator = 0;
	float mFixedTimeStep = 1.f/60.f;

	float mTotalTime = 0;
	float mDeltaTime = 0;

	chrono::high_resolution_clock mClock;
	chrono::high_resolution_clock::time_point mStartTime;
	chrono::high_resolution_clock::time_point mLastFrame;

public:
  NodeDelegate<CommandBuffer&> OnFixedUpdate;
  NodeDelegate<CommandBuffer&> OnUpdate;
	
	inline PhysicsSim(Scene& scene, const string& name) : Scene::Node(scene, name) {
		mStartTime = mLastFrame = mClock.now();
	}

  inline void Update(CommandBuffer& commandBuffer) {
		ProfilerRegion ps("PhysicsSim::Update");
		auto t1 = mClock.now();
		mDeltaTime = (t1 - mLastFrame).count() * 1e-9f;
		mTotalTime = (t1 - mStartTime).count() * 1e-9f;
		mLastFrame = t1;

		{
			ProfilerRegion ps("Fixed Update");
			mFixedAccumulator += mDeltaTime;
			float fixedTime = 0;
			t1 = mClock.now();
			while (mFixedAccumulator > mFixedTimeStep && fixedTime < mFixedTimeLimitPerFrame) {
				OnFixedUpdate(commandBuffer);
				mFixedAccumulator -= mFixedTimeStep;
				fixedTime = (mClock.now() - t1).count() * 1e-9f;
			}
		}
		OnUpdate(commandBuffer);
	}

	inline void FixedTimeStep(float step) { mFixedTimeStep = step; }
	inline void PhysicsTimeLimitPerFrame(float t) { mPhysicsTimeLimitPerFrame = t; }
	
	inline float FixedTimeStep() const { return mFixedTimeStep; }
	inline float FixedTimeLimitPerFrame() const { return mPhysicsTimeLimitPerFrame; }

	inline float TotalTime() const { return mTotalTime; }
	inline float DeltaTime() const { return mDeltaTime; }
};

}
