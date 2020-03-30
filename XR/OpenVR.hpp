#pragma once

#include <openvr.h>

#include "XRRuntime.hpp"
#include <Scene/Scene.hpp>
#include <Scene/MeshRenderer.hpp>

class OpenVR : public XRRuntime {
public:
    ENGINE_EXPORT OpenVR();
    ENGINE_EXPORT ~OpenVR();

    ENGINE_EXPORT bool Init() override;
    ENGINE_EXPORT bool InitScene(Scene* scene) override;

    ENGINE_EXPORT void PreInstanceInit(Instance* instance);
    ENGINE_EXPORT void PreDeviceInit(Instance* instance, VkPhysicalDevice device);

    ENGINE_EXPORT void BeginFrame();
    ENGINE_EXPORT void PostRender(CommandBuffer* commandBuffer);
    ENGINE_EXPORT void EndFrame();

private:
    Scene* mScene;
    Camera* mHmdCamera;

    vr::IVRSystem* mSystem;
    vr::IVRRenderModels* mRenderModelInterface;

    Texture* mCopyTarget;

    Object* mTrackedObjects[vr::k_unMaxTrackedDeviceCount];

    std::unordered_map<std::string, std::pair<Mesh*, vr::TextureID_t>> mRenderModels;
    std::unordered_map<vr::TextureID_t, std::pair<Texture*, std::shared_ptr<Material>>> mRenderModelMaterials;

    vr::TrackedDevicePose_t mTrackedDevices[vr::k_unMaxTrackedDeviceCount];
    vr::TrackedDevicePose_t mTrackedDevicesPredicted[vr::k_unMaxTrackedDeviceCount];
};