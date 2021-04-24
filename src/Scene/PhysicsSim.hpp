#pragma once

#include "Scene.hpp"

namespace stm {

template<typename duration_t = chrono::duration<float>> requires(is_specialization_v<duration_t, chrono::duration>)
class PhysicsSim : public Scene::Node {
private:
	duration_t mTimeLimit = 30ms;
	duration_t mFixedTimeStep = 1ms;
	duration_t mFixedAccumulator = 0;

	duration_t mTotalTime = 0;
	duration_t mDeltaTime = 0;

	chrono::high_resolution_clock::time_point mStartTime;
	chrono::high_resolution_clock::time_point mLastFrame;

public:
  Event<CommandBuffer&> OnFixedUpdate;
  Event<CommandBuffer&> OnUpdate;
	
	inline PhysicsSim(Scene& scene, const string& name) : Scene::Node(scene, name), OnFixedUpdate(this), OnUpdate(this) {
		mStartTime = mLastFrame = chrono::high_resolution_clock::now();
	}

	inline void fixed_time_step(const auto& step) { mFixedTimeStep = step; }
	inline const auto& fixed_time_step() const { return mFixedTimeStep; }
	inline void time_limit(const auto& t) { mTimeLimit = t; }
	inline const auto& time_limit() const { return mTimeLimit; }
	inline const auto& total_time() const { return mTotalTime; }
	inline const auto& delta_time() const { return mDeltaTime; }

  inline void update(CommandBuffer& commandBuffer) {
		ProfilerRegion ps("PhysicsSim::update", commandBuffer);
		
		auto t1 = chrono::high_resolution_clock::now();
		mDeltaTime = duration_cast<duration_t>(t1 - mLastFrame);
		mTotalTime = duration_cast<duration_t>(t1 - mStartTime);
		mLastFrame = t1;

		mFixedAccumulator += mDeltaTime;
		while (mFixedAccumulator > mFixedTimeStep) {
			OnFixedUpdate(commandBuffer);
			mFixedAccumulator -= mFixedTimeStep;
			if (mTimeLimit < chrono::high_resolution_clock::now() - mLastFrame) break;
		}

		OnUpdate(commandBuffer);
	}
};

}
