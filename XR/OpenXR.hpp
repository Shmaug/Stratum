#pragma once

#include "XRRuntime.hpp"

#include <openxr/openxr.h>

class OpenXR : public XRRuntime {
public:
    ENGINE_EXPORT OpenXR();
    ENGINE_EXPORT ~OpenXR();
    
    ENGINE_EXPORT bool Init() override;
    ENGINE_EXPORT bool InitScene(Scene* scene) override;

    ENGINE_EXPORT void PreInstanceInit(Instance* instance);
    ENGINE_EXPORT void PreDeviceInit(Instance* instance, VkPhysicalDevice device);

    ENGINE_EXPORT void PollEvents() override;

private:
    XrInstance mInstance;
    XrSystemId mSystem;
    XrSession mSession;
    XrSystemProperties mSystemProperties;
    XrViewConfigurationType mViewConfiguration;

    Scene* mScene;
    Framebuffer* mHmdFramebuffer;
    Camera* mHmdCamera;
};
