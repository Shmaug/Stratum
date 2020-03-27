#pragma once

#include "XRRuntime.hpp"

#include <openvr.h>

class OpenVR : public XRRuntime {
public:
    ENGINE_EXPORT OpenVR();
    ENGINE_EXPORT ~OpenVR();

    ENGINE_EXPORT bool Init() override;
    ENGINE_EXPORT bool InitScene(Scene* scene) override;

private:
    vr::IVRSystem* mSystem;
};