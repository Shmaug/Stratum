#pragma once

#ifdef STRATUM_ENABLE_OPENXR

#include "Scene.hpp"

#include <openxr/openxr.hpp>

namespace stm {

class XRScene {
public:
  STRATUM_API XRScene(Node& node);
  inline ~XRScene() { destroy(); }

  inline const xr::Instance& instance() const { return mInstance; }
  inline const xr::SystemId& system() const { return mSystem; }

  STRATUM_API void poll_actions();

  STRATUM_API Image::View back_buffer();

  STRATUM_API void present();

  STRATUM_API void get_vulkan_extensions(string& instanceExtensions, string& deviceExtensions);

private:
  STRATUM_API void destroy();

  xr::DispatchLoaderDynamic mDispatch;
  xr::DebugUtilsMessengerEXT mDebugMessenger;

  Node& mNode;

  vector<component_ptr<Camera>> mViews;

  xr::Instance mInstance;
  xr::SystemId mSystem;
  xr::Session mSession;

  vk::Format mSwapchainFormat;
  std::vector<xr::Swapchain> mSwapchains;
  std::vector<xr::SwapchainImageVulkanKHR*> mSwapchainImages;

  xr::Space mReferenceSpace;
  // Spaces for hands, etc
  std::vector<xr::Space> mActionSpaces;
  std::vector<xr::Path> mHandPaths;
  xr::ActionSet mActionSet;
  xr::Action mGrabAction;
  xr::Action mPoseAction;

  xr::FrameState mFrameState;
  std::vector<xr::CompositionLayerProjectionView> mProjectionViews;

  xr::ViewConfigurationType mViewConfiguration;
  xr::ReferenceSpaceType mReferenceSpaceType;

  xr::SystemProperties mSystemProperties;
};

}

#endif