#include "Application.hpp"
#include "RasterScene.hpp"

using namespace stm;
using namespace stm::hlsl;

Application::Application(Node& node, Window& window) : mNode(node), mWindow(window) {
  mWindowRenderNode = mNode.make_child(node.name() + " DynamicRenderPass").make_component<DynamicRenderPass>();
  mMainPass = mWindowRenderNode->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mWindowRenderNode, "mainPass"));

  vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
  if (auto msaa = mWindow.mInstance.find_argument("msaa")) {
    if      (*msaa == "2")  sampleCount = vk::SampleCountFlagBits::e2;
    else if (*msaa == "4")  sampleCount = vk::SampleCountFlagBits::e4;
    else if (*msaa == "8")  sampleCount = vk::SampleCountFlagBits::e8;
    else if (*msaa == "16") sampleCount = vk::SampleCountFlagBits::e16;
    else if (*msaa == "32") sampleCount = vk::SampleCountFlagBits::e32;
    if (sampleCount != vk::SampleCountFlagBits::e1)
      mMainPass->emplace_attachment("colorMS", AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription({},
        mWindow.surface_format().format, sampleCount,
        vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal));
  }

  auto& backBuffer = mMainPass->emplace_attachment("backBuffer", (sampleCount == vk::SampleCountFlagBits::e1) ? AttachmentType::eColor : AttachmentType::eResolve, blend_mode_state(),
    vk::AttachmentDescription({},
      mWindow.surface_format().format, vk::SampleCountFlagBits::e1,
      (sampleCount == vk::SampleCountFlagBits::e1) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR));
    
  mMainPass->emplace_attachment("depthBuffer", AttachmentType::eDepthStencil, blend_mode_state(), vk::AttachmentDescription({},
    vk::Format::eD32Sfloat, sampleCount,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal) );

  mNode.listen(mMainPass->OnDraw, [&](CommandBuffer& commandBuffer) {
    vk::Extent2D extent = mWindow.swapchain_extent();
    commandBuffer->setViewport(0, vk::Viewport(0, (float)extent.height, (float)extent.width, -(float)extent.height, 0, 1));
    commandBuffer->setScissor(0, vk::Rect2D({}, extent));
  }, EventPriority::eFirst);
}

void Application::loop() {
  vector<pair<Texture::View, Texture::View>> renderBuffers(mWindow.back_buffer_count());

  size_t frameCount = 0;
  auto t0 = chrono::high_resolution_clock::now();
  while (true) {
    ProfilerRegion ps("Frame " + to_string(frameCount++));

    {
      ProfilerRegion ps("Application::PreFrame");
      PreFrame();
    }

    mWindow.mInstance.poll_events();
    if (!mWindow.handle())
      break;

    auto t1 = chrono::high_resolution_clock::now();
    float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
    t0 = t1;

    auto commandBuffer = mWindow.mInstance.device().get_command_buffer("Frame");
    
    {
      ProfilerRegion ps("Application::OnUpdate");
      OnUpdate(*commandBuffer, deltaTime);
    }
    
    if (mWindow.back_buffer_usage() != mBackBufferUsage)
      mWindow.back_buffer_usage(mWindow.back_buffer_usage() | mBackBufferUsage);
    
    if (auto backBuffer = mWindow.acquire_image(*commandBuffer)) {
      auto&[depthBuffer, colorMS] = renderBuffers[mWindow.back_buffer_index()];
      
      if (!depthBuffer || depthBuffer.texture()->usage() != mDepthBufferUsage || depthBuffer.texture()->extent() != mWindow.back_buffer().texture()->extent())
        if (auto desc = mMainPass->find_attachment("depthBuffer")) {
          ProfilerRegion ps("make depth attachment");
          depthBuffer = make_shared<Texture>(commandBuffer->mDevice, "depthBuffer", backBuffer.texture()->extent(), desc->mDescription, mDepthBufferUsage);
        }
      if (!colorMS || colorMS.texture()->extent() != mWindow.back_buffer().texture()->extent())
        if (auto desc = mMainPass->find_attachment("colorMS")) {
          ProfilerRegion ps("make colorMS attachment");
          colorMS = make_shared<Texture>(commandBuffer->mDevice, "colorMS", backBuffer.texture()->extent(), desc->mDescription, vk::ImageUsageFlagBits::eColorAttachment);
        }
      
      unordered_map<RenderAttachmentId, pair<Texture::View, vk::ClearValue>> attachments {
        { "backBuffer", { backBuffer, vk::ClearColorValue(std::array<float,4>{0,0,0,0}) } },
        { "depthBuffer", { depthBuffer, vk::ClearDepthStencilValue(0.f, 0) } },
        { "colorMS", { colorMS, {}}}
      };
      mMainPass->mPass.render(*commandBuffer, attachments);

      backBuffer.transition_barrier(*commandBuffer, vk::ImageLayout::ePresentSrcKHR);

      pair<shared_ptr<Semaphore>, vk::PipelineStageFlags> waits(mWindow.image_available_semaphore(), vk::PipelineStageFlagBits::eColorAttachmentOutput);
      auto semaphore = make_shared<Semaphore>(commandBuffer->mDevice, "RenderSemaphore");
      commandBuffer->mDevice.submit(commandBuffer, waits, semaphore);
      mWindow.present(**semaphore);
    }
  }
}