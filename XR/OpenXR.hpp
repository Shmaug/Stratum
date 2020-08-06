#pragma once

#include "XRRuntime.hpp"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "PointerRenderer.hpp"
#include <Scene/MeshRenderer.hpp>
#include <Scene/Scene.hpp>

class OpenXR : public XRRuntime {
public:
    STRATUM_API OpenXR();
    STRATUM_API ~OpenXR();
    
    STRATUM_API bool OnSceneInit(Scene* scene) override;

    STRATUM_API std::set<std::string> InstanceExtensionsRequired() override;
    STRATUM_API std::set<std::string> DeviceExtensionsRequired(VkPhysicalDevice device) override;

    STRATUM_API void OnFrameStart() override;
    STRATUM_API void PostRender(CommandBuffer* commandBuffer) override;
    STRATUM_API void OnFrameEnd() override;

private:
    bool mInitialized;
    XrInstance mInstance;
    XrSystemId mSystem;
    XrSession mSession;

    VkFormat mSwapchainFormat;
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
