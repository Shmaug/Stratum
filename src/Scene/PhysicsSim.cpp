#include "PhysicsSim.hpp"

using namespace stm;

void PhysicsSim::Update(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("PhysicsSim::Update");
	
	auto t1 = mClock.now();
	mDeltaTime = (t1 - mLastFrame).count() * 1e-9f;
	mTotalTime = (t1 - mStartTime).count() * 1e-9f;
	mLastFrame = t1;
	
	{
		ProfilerRegion ps("Fixed Update");
		mFixedAccumulator += mDeltaTime;
		float physicsTime = 0;
		t1 = mClock.now();
		while (mFixedAccumulator > mFixedTimeStep && physicsTime < mPhysicsTimeLimitPerFrame) {
			ranges::for_each(mPlugins, [&](Plugin* p){ p->OnFixedUpdate(commandBuffer); });
			for (const auto& o : mObjects) if (o->Enabled()) o->OnFixedUpdate(commandBuffer);
			mFixedAccumulator -= mFixedTimeStep;
			physicsTime = (mClock.now() - t1).count() * 1e-9f;
		}
	}

	{
		ProfilerRegion ps("OnUpdate(), OnLateUpdate()");
		ranges::for_each(mPlugins, [&](Plugin* p){ p->OnUpdate(commandBuffer); });
		for (auto& o : mObjects) if (o->Enabled()) o->OnUpdate(commandBuffer);
		ranges::for_each(mPlugins, [&](Plugin* p){ p->OnLateUpdate(commandBuffer); });
		for (auto& o : mObjects) if (o->Enabled()) o->OnLateUpdate(commandBuffer);
	}
}