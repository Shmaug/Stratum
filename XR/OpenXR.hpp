#pragma once

#include "XRRuntime.hpp"

#include <Scene/Scene.hpp>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class OpenXR : public XRRuntime {
public:
    ENGINE_EXPORT OpenXR();
    ENGINE_EXPORT ~OpenXR();
    
    ENGINE_EXPORT bool Init() override;
    ENGINE_EXPORT bool InitScene(Scene* scene) override;

    ENGINE_EXPORT void PreInstanceInit(Instance* instance);
    ENGINE_EXPORT void PreDeviceInit(Instance* instance, VkPhysicalDevice device);

    ENGINE_EXPORT void BeginFrame() override;
    ENGINE_EXPORT void PostRender(CommandBuffer* commandBuffer) override;
    ENGINE_EXPORT void EndFrame() override;

private:
    XrInstance mInstance;
    XrSystemId mSystem;
    XrSession mSession;

    bool mVisible;

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

    ENGINE_EXPORT void Cleanup();

    ENGINE_EXPORT void CreateSession();
    ENGINE_EXPORT bool XR_FAILED_MSG(XrResult result, const std::string& errmsg);
};
