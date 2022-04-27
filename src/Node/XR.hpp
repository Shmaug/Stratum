#pragma once

#include "Scene.hpp"

#ifdef STRATUM_ENABLE_OPENXR
#include <openxr/openxr_platform.h>
#include <openxr/openxr.hpp>

namespace stm {

class XR {
public:
  struct View {
    component_ptr<TransformData> mTransform;
    component_ptr<Camera> mCamera;
    inline const Image::View& back_buffer() const { return mSwapchainImages[mImageIndex]; }
  private:
    friend class XR;
    xr::Swapchain mSwapchain;
    vector<Image::View> mSwapchainImages;
    uint32_t mImageIndex;
  };

  STRATUM_API XR(Node& node);
  inline ~XR() { destroy(); }

  inline const xr::Instance& instance() const { return mInstance; }
  inline const xr::SystemId& system() const { return mSystem; }
  inline const vector<View>& views() const { return mViews; }

	inline const vk::ImageUsageFlags& back_buffer_usage() const { return mSwapchainImageUsage; }
	inline vk::ImageUsageFlags& back_buffer_usage() { return mSwapchainImageUsage; }

  inline const xr::SessionState& state() const { return mSessionState; }

  STRATUM_API void get_vulkan_extensions(string& instanceExtensions, string& deviceExtensions);
  STRATUM_API vk::PhysicalDevice get_vulkan_device(Instance& instance);
  STRATUM_API void create_session(Instance& instance);

  STRATUM_API void poll_events();
  STRATUM_API void render(CommandBuffer& commandBuffer);
  STRATUM_API void present();

  NodeEvent<CommandBuffer&> OnRender;

private:
  STRATUM_API void create_views();
  STRATUM_API void destroy();

  Node& mNode;

  xr::DispatchLoaderDynamic mDispatch;
  xr::DebugUtilsMessengerEXT mDebugMessenger;

  Device::QueueFamily* mQueueFamily;

  xr::Instance mInstance;
  xr::SystemId mSystem;
  xr::Session mSession;

  xr::Space mReferenceSpace;
  xr::ViewConfigurationType mPrimaryViewConfiguration;
  vk::ImageUsageFlags mSwapchainImageUsage;
  vector<View> mViews;
  vector<xr::View> mXRViews;

  xr::CompositionLayerProjection mCompositionLayer;
  xr::FrameState mFrameState;
  xr::SessionState mSessionState;
  bool mSessionRunning;

  // Spaces for hands, etc
  vector<xr::Space> mActionSpaces;
  vector<xr::Path> mHandPaths;
  xr::ActionSet mActionSet;
  xr::Action mGrabAction;
  xr::Action mPoseAction;
};

}

#endif