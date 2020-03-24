#pragma once

#include "XRRuntime.hpp"

#include <openxr/openxr.h>

class OpenXRRuntime : public XRRuntime {
public:
    virtual bool Init() override;

private:
    XrInstance mInstance;
};
