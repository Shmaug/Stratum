#include "XR.hpp"
#include <Scene/Scene.hpp>
#include <Scene/GUI.hpp>
#include <Content/Font.hpp>

using namespace std;

ENGINE_PLUGIN(XR)

XR::XR() : mScene(nullptr), mRuntime(nullptr) {
	mEnabled = true;
}
XR::~XR() {
	safe_delete(mRuntime);
	for (Object* o : mObjects)
		mScene->RemoveObject(o);
}

bool XR::Init(Scene* scene) {
	mScene = scene;

	return true;
}

void XR::Update(CommandBuffer* commandBuffer) {

}

void XR::DrawGizmos(CommandBuffer* commandBuffer, Camera* camera) {

}