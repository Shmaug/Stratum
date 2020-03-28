#include "OpenVR.hpp"

#include <Scene/Scene.hpp>

using namespace std;
using namespace vr;

OpenVR::OpenVR() : mSystem(nullptr) {}
OpenVR::~OpenVR() {
    if (mSystem) VR_Shutdown();
}

bool OpenVR::Init() {
    if (!VR_IsRuntimeInstalled()) {
        printf_color(COLOR_YELLOW, "%s", "OpenVR runtime is not installed\n");
        return false;
    }

    HmdError err;
    mSystem = VR_Init(&err, VRApplication_Scene);
    if (!mSystem) {
        printf_color(COLOR_YELLOW, "%s: %s\n", "Failed to initialize OpenVR", VR_GetVRInitErrorAsEnglishDescription(err));
        return false;
    }

    return false;
}
bool OpenVR::InitScene(Scene* scene) {

    return true;
}