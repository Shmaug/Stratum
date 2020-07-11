#include "CameraControl.hpp"
#include <Scene/Scene.hpp>
#include <Scene/GUI.hpp>
#include <Content/Font.hpp>

using namespace std;

ENGINE_PLUGIN(CameraControl)

CameraControl::CameraControl()
	: mScene(nullptr), mCameraPivot(nullptr), mInput(nullptr), mCameraDistance(1.5f), mCameraEuler(float3(0)), mShowPerformance(false) {
	mEnabled = true;
}
CameraControl::~CameraControl() {
	for (Camera* c : mCameras)
		mScene->RemoveObject(c);
	mScene->RemoveObject(mCameraPivot);
}

bool CameraControl::Init(Scene* scene) {
	mScene = scene;
	mInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

	shared_ptr<Object> cameraPivot = make_shared<Object>("CameraPivot");
	mScene->AddObject(cameraPivot);
	mCameraPivot = cameraPivot.get();
	mCameraPivot->LocalPosition(0, .5f, 0);

	shared_ptr<Camera> camera = make_shared<Camera>("Camera", mScene->Instance()->Window());
	mScene->AddObject(camera);
	camera->Near(.1f);
	camera->Far(1024.f);
	camera->FieldOfView(radians(65.f));
	camera->LocalPosition(0, 0, -mCameraDistance);
	mCameras.push_back(camera.get());
	mCameraPivot->AddChild(camera.get());

	return true;
}

void CameraControl::Update(CommandBuffer* commandBuffer) {
	if (mInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;
	if (mInput->KeyDownFirst(KEY_F1)) mScene->DrawGizmos(!mScene->DrawGizmos());

	if (mInput->GetPointerLast(0)->mGuiHitT < 0){
		#pragma region Camera control
		if (mInput->KeyDown(MOUSE_MIDDLE) || (mInput->KeyDown(MOUSE_LEFT) && mInput->KeyDown(KEY_LALT))) {
			float3 md = mInput->CursorDelta();
			if (mInput->KeyDown(KEY_LSHIFT)) {
				md.x = -md.x;
				md = md * .0005f * mCameraDistance;
			} else
				md = float3(md.y, md.x, 0) * .005f;

			if (mInput->KeyDown(KEY_LSHIFT))
				// translate camera
				mCameraPivot->LocalPosition(mCameraPivot->LocalPosition() + mCameraPivot->LocalRotation() * md);
			else {
				mCameraEuler += md;
				mCameraEuler.x = clamp(mCameraEuler.x, -PI * .5f, PI * .5f);
				// rotate camera
			}
			mCameraPivot->LocalRotation(quaternion(mCameraEuler));
		}
		mCameraDistance *= 1.0f - .2f * mInput->ScrollDelta();
		mCameraDistance = max(.01f, mCameraDistance);

		for (uint32_t i = 0; i < mCameras.size(); i++)
			mCameras[i]->LocalPosition(0, 0, -mCameraDistance);
		#pragma endregion
	}
}

void CameraControl::PreRenderScene(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {
	if (pass != PASS_MAIN || camera != mScene->Cameras()[0]) return;
	
	#ifdef PROFILER_ENABLE
	if (mShowPerformance)
		Profiler::DrawProfiler(mScene);
	#endif
}