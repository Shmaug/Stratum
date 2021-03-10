#pragma once

#include "Camera.hpp"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace stm {

class XR {
public:
    STRATUM_API XR();
    STRATUM_API ~XR();
    
    STRATUM_API bool Init(Scene* scene);

    STRATUM_API unordered_set<string> InstanceExtensionsRequired();
    STRATUM_API unordered_set<string> DeviceExtensionsRequired(vk::PhysicalDevice device);

    STRATUM_API void OnFrameStart();
    STRATUM_API void PostRender(CommandBuffer& commandBuffer);
    STRATUM_API void OnFrameEnd();

private:
    bool mInitialized = false;
    XrInstance mInstance = XR_NULL_HANDLE;
    XrSystemId mSystem = 0;
    XrSession mSession = XR_NULL_HANDLE;

    vk::Format mSwapchainFormat;
    vector<XrSwapchain> mSwapchains;
    vector<XrSwapchainImageVulkanKHR*> mSwapchainImages;
    uint32_t mViewCount = 0;

    XrSpace mReferenceSpace = XR_NULL_HANDLE;
    // Spaces for hands, etc
    vector<XrSpace> mActionSpaces;
    vector<XrPath> mHandPaths;
    XrActionSet mActionSet = XR_NULL_HANDLE;
	XrAction mGrabAction = XR_NULL_HANDLE;
	XrAction mPoseAction = XR_NULL_HANDLE;

    XrFrameState mFrameState;
    vector<XrCompositionLayerProjectionView> mProjectionViews;
    
    XrViewConfigurationType mViewConfiguration;
    XrReferenceSpaceType mReferenceSpaceType;

    XrSystemProperties mSystemProperties;

    Scene* mScene = nullptr;
    Framebuffer* mHmdFramebuffer = nullptr;
    Camera* mHmdCamera = nullptr;

    STRATUM_API void Cleanup();

    STRATUM_API void CreateSession();
    STRATUM_API bool FailMsg(XrResult result, const string& errmsg);
};

}