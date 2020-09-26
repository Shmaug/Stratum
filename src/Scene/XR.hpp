#pragma once

#include <openxr/openxr.h>

#include "Scene.hpp"
#include "Renderers/PointerRenderer.hpp"

struct XrSwapchainImageVulkanKHR;

namespace stm {

class XR {
public:
    STRATUM_API XR();
    STRATUM_API ~XR();
    
    STRATUM_API bool OnSceneInit(Scene* scene);

    STRATUM_API std::set<std::string> InstanceExtensionsRequired();
    STRATUM_API std::set<std::string> DeviceExtensionsRequired(vk::PhysicalDevice device);

    STRATUM_API void OnFrameStart();
    STRATUM_API void PostRender(CommandBuffer& commandBuffer);
    STRATUM_API void OnFrameEnd();

private:
    bool mInitialized = false;
    XrInstance mInstance = XR_NULL_HANDLE;
    XrSystemId mSystem = 0;
    XrSession mSession = XR_NULL_HANDLE;

    vk::Format mSwapchainFormat;
    std::vector<XrSwapchain> mSwapchains;
    std::vector<XrSwapchainImageVulkanKHR*> mSwapchainImages;
    uint32_t mViewCount = 0;

    XrSpace mReferenceSpace = XR_NULL_HANDLE;
    // Spaces for hands, etc
    std::vector<XrSpace> mActionSpaces;
    std::vector<XrPath> mHandPaths;
    XrActionSet mActionSet = XR_NULL_HANDLE;
	XrAction mGrabAction = XR_NULL_HANDLE;
	XrAction mPoseAction = XR_NULL_HANDLE;

    XrFrameState mFrameState;
    std::vector<XrCompositionLayerProjectionView> mProjectionViews;
    
    XrViewConfigurationType mViewConfiguration;
    XrReferenceSpaceType mReferenceSpaceType;

    XrSystemProperties mSystemProperties;

    Scene* mScene = nullptr;
    Framebuffer* mHmdFramebuffer = nullptr;
    Camera* mHmdCamera = nullptr;

    STRATUM_API void Cleanup();

    STRATUM_API void CreateSession();
    STRATUM_API bool FailMsg(XrResult result, const std::string& errmsg);
};

}