# Stratum

High performance modular plugin-based Vulkan rendering engine in C++17 with minimal dependencies.

## How to Build
run `build/setup.bat` or `build/setup.sh`, then build Stratum with CMake

# Rendering Overview
## Note: Stratum uses a **left-handed** coordinate system!

# Pipeline Overview
Stratum provides a custom shader compiler. It uses SPIRV reflection and custom directives to support automatic generation of data such as descriptor and pipeline layouts. It also provides macro variants, similar to Unity. Here is a list of all supported directives:

## Stereo Rendering
Cameras have a `StereoMode` property which is implemented by rendering twice, calling `Camera::SetStereo()` to set the left/right eyes before rendering each time