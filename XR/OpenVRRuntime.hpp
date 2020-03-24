#pragma once

#include "XRRuntime.hpp"

#include <openvr.h>

class OpenVRRuntime : public XRRuntime {
public:
    virtual bool Init() override;

private:
};