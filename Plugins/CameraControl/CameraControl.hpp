#pragma once

#include <Core/EnginePlugin.hpp>
#include <Scene/Scene.hpp>
#include <Input/MouseKeyboardInput.hpp>
#include <Util/Profiler.hpp>

class CameraControl : public EnginePlugin {
private:
	Scene* mScene;
	float mCameraDistance;
	float3 mCameraEuler;

	MouseKeyboardInput* mInput;

	Object* mCameraPivot;
	std::vector<Camera*> mCameras;

	bool mShowPerformance;

public:
	PLUGIN_EXPORT CameraControl();
	PLUGIN_EXPORT ~CameraControl();

	PLUGIN_EXPORT bool Init(Scene* scene) override;
	PLUGIN_EXPORT void Update(CommandBuffer* commandBuffer) override;
	PLUGIN_EXPORT void PreRenderScene(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;

	inline void CameraDistance(float d) { mCameraDistance = d; }
	inline float CameraDistance() const { return mCameraDistance; }
	inline float3 CameraEuler() const { return mCameraEuler; }
	inline void CameraEuler(const float3& e) { mCameraEuler = e; }
	inline Object* CameraPivot() const { return mCameraPivot; }

	inline int Priority() override { return 1000; }
};