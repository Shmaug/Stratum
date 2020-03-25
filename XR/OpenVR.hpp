#pragma once

#include "XRRuntime.hpp"

#include <openvr.h>

class OpenVR : public XRRuntime {
public:
    ENGINE_EXPORT OpenVR();
    ENGINE_EXPORT ~OpenVR();

    ENGINE_EXPORT virtual bool Init() override;

private:
    vr::IVRSystem* mSystem;
};