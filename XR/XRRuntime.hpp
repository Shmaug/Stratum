#pragma once

#include <Util/Util.hpp>

class XRRuntime {
public:
    inline virtual ~XRRuntime() {};

protected:
    friend class Instance;
    friend class Scene;
    inline virtual std::set<std::string> InstanceExtensionsRequired() { return {}; };
    inline virtual std::set<std::string> DeviceExtensionsRequired(VkPhysicalDevice device) { return {}; };

    // Called after the Vulkan instance and device are created, before the scene loop starts
    inline virtual bool OnSceneInit(Scene* scene) { return false; };

    inline virtual void OnFrameStart() {}
    // Called after the scene has rendered, including resolve/copy to the target window.
    // Scene camera resolve buffers are in VK_IMAGE_LAYOUT_GENERAL
    inline virtual void PostRender(CommandBuffer* commandBuffer) {}

    // Called after command buffers are executed, just before the window presents
    inline virtual void OnFrameEnd() {}
};