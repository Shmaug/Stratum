#pragma once

#include "Scene.hpp"

#ifdef STRATUM_ENABLE_OPENXR
#include <openxr/openxr.hpp>

namespace stm {

class XR {
public:
  struct View {
    component_ptr<hlsl::TransformData> mTransform;
    component_ptr<Camera> mCamera;
    vk::Rect2D mImageRect;
  };

  STRATUM_API XR(Node& node);
  inline ~XR() { destroy(); }

  inline const xr::Instance& instance() const { return mInstance; }
  inline const xr::SystemId& system() const { return mSystem; }
  inline const vector<View>& views() const { return mViews; }
  
	inline Image::View back_buffer() const { return mSwapchainImages[mSwapchainImageIndex]; }
	inline const vk::ImageUsageFlags& back_buffer_usage() const { return mSwapchainImageUsage; }
	inline vk::ImageUsageFlags& back_buffer_usage() { return mSwapchainImageUsage; }

  STRATUM_API void get_vulkan_extensions(string& instanceExtensions, string& deviceExtensions);
  STRATUM_API void create_session(Device::QueueFamily& queueFamily);
  STRATUM_API void create_swapchain();

  STRATUM_API void poll_actions();
  STRATUM_API void do_frame(CommandBuffer& commandBuffer);

  NodeEvent<CommandBuffer&> OnRender;

private:
  STRATUM_API void destroy();

  Node& mNode;

  xr::DispatchLoaderDynamic mDispatch;
  xr::DebugUtilsMessengerEXT mDebugMessenger;

  Device::QueueFamily* mQueueFamily;
  
  xr::Instance mInstance;
  xr::SystemId mSystem;
  xr::Session mSession;
  xr::Swapchain mSwapchain;
  vector<shared_ptr<Image>> mSwapchainImages;
  uint32_t mSwapchainImageIndex;
  vk::ImageUsageFlags mSwapchainImageUsage;

  xr::Space mReferenceSpace;
  xr::ViewConfigurationType mPrimaryViewConfiguration;
  vector<View> mViews;
  
  // Spaces for hands, etc
  std::vector<xr::Space> mActionSpaces;
  std::vector<xr::Path> mHandPaths;
  xr::ActionSet mActionSet;
  xr::Action mGrabAction;
  xr::Action mPoseAction;
};

}

#endif