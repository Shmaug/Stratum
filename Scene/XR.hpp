#pragma once

#include <Scene/Scene.hpp>
#include <Scene/Renderers/PointerRenderer.hpp>

#include <openxr/openxr_platform.h>

class XR {
public:
    STRATUM_API XR();
    STRATUM_API ~XR();
    
    STRATUM_API bool OnSceneInit(Scene* scene);

    STRATUM_API std::set<std::string> InstanceExtensionsRequired();
    STRATUM_API std::set<std::string> DeviceExtensionsRequired(vk::PhysicalDevice device);

    STRATUM_API void OnFrameStart();
    STRATUM_API void PostRender(stm_ptr<CommandBuffer> commandBuffer);
    STRATUM_API void OnFrameEnd();

private:
    bool mInitialized;
    XrInstance mInstance;
    XrSystemId mSystem;
    XrSession mSession;

    vk::Format mSwapchainFormat;
    std::vector<XrSwapchain> mSwapchains;
    std::vector<XrSwapchainImageVulkanKHR*> mSwapchainImages;
    uint32_t mViewCount;

    XrSpace mReferenceSpace;
    // Spaces for hands, etc
    std::vector<XrSpace> mActionSpaces;
    std::vector<XrPath> mHandPaths;
    XrActionSet mActionSet;
	XrAction mGrabAction;
	XrAction mPoseAction;

    XrFrameState mFrameState;
    std::vector<XrCompositionLayerProjectionView> mProjectionViews;
    
    XrViewConfigurationType mViewConfiguration;
    XrReferenceSpaceType mReferenceSpaceType;

    XrSystemProperties mSystemProperties;

    Scene* mScene;
    Framebuffer* mHmdFramebuffer;
    Camera* mHmdCamera;

    STRATUM_API void Cleanup();

    STRATUM_API void CreateSession();
    STRATUM_API bool XR_FAILED_MSG(XrResult result, const std::string& errmsg);
};
