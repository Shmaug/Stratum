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

	{
		ProfilerRegion ps("Scene Lighting");
		mLighting.mLightBuffer = commandBuffer.GetBuffer("Light Buffer", sizeof(LightData), vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		if (mLights.empty()) {
			mLighting.mShadowAtlas = commandBuffer.GetTexture("Shadow Atlas", { 1, 1, 1 }, vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
		} else {
			ProfilerRegion ps("Gather Lights and Shadows");
			mLighting.mShadowAtlas = commandBuffer.GetTexture("Shadow Atlas", { 4096, 4096, 1 }, vk::Format::eD32Sfloat, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);

			AlignedBox3f shadowBounds(Vector3f::Constant(numeric_limits<float>::infinity()), Vector3f::Constant(-numeric_limits<float>::infinity()));
			for (uint32_t i = 0; i < mRenderers.size(); i++) {
				auto bounds = mRenderers[i]->Bounds();
				if (bounds && mRenderers[i]->Visible("forward/depth")) {
					bounds->mMin -= 1e-2f;
					bounds->mMax += 1e-2f;
					shadowBounds.extend(*bounds);
				}
			}
			uint32_t si = 0;
			uint32_t shadowCount = 0;

			Vector3f sceneCenter = shadowBounds.Center();
			Vector3f sceneExtent = shadowBounds.HalfSize();
			float shadowExtentMax = fmaxf(fmaxf(sceneExtent.x, sceneExtent.y), sceneExtent.z) * 1.73205080757f; // sqrt(3)*x

			mLighting.mLightCount = 0;
			LightData* lights = (LightData*)mLighting.mLightBuffer->Mapped();

			for (Light* l : mLights) {
				if (!l->Enabled()) continue;
				float cosInner = cos(l->InnerSpotAngle());
				float cosOuter = cos(l->OuterSpotAngle());
				lights[mLighting.mLightCount].ToLight = l->InverseTransform();
				lights[mLighting.mLightCount].Emission = l->Color() * l->Intensity();
				lights[mLighting.mLightCount].Type_ShadowIndex = (uint32_t)l->Type();
				lights[mLighting.mLightCount].SpotAngleScale = 1.f / fmaxf(.001f, cosInner - cosOuter);
				lights[mLighting.mLightCount].SpotAngleOffset = -cosOuter * lights[mLighting.mLightCount].SpotAngleScale;

				mLighting.mLightCount++;
				if (mLighting.mLightCount >= gMaxLightCount) break;
			}
		}
		commandBuffer.TransitionBarrier(*mLighting.mShadowAtlas, vk::ImageLayout::eShaderReadOnlyOptimal);
	}
}