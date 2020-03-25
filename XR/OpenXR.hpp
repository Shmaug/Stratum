#pragma once

#include "XRRuntime.hpp"

#include <openxr/openxr.h>

class OpenXR : public XRRuntime {
public:
    ENGINE_EXPORT ~OpenXR();
    
    ENGINE_EXPORT virtual bool Init() override;

private:
    XrInstance mInstance;
};
