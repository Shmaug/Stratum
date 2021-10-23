#include "Application.hpp"
#include "RasterScene.hpp"

#include <imgui.h>

using namespace stm;
using namespace stm::hlsl;

Application::Application(Node& node, Window& window) : mNode(node), mWindow(window) {
  mWindowRenderNode = mNode.make_child(node.name() + " DynamicRenderPass").make_component<DynamicRenderPass>();
  mMainPass = mWindowRenderNode->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mWindowRenderNode, "mainPass"));

  vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
  mMainPass->emplace_attachment("colorBuffer", AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription({},
    mWindow.surface_format().format, sampleCount,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal));
  mMainPass->emplace_attachment("depthBuffer", AttachmentType::eDepthStencil, blend_mode_state(), vk::AttachmentDescription({},
    vk::Format::eD32Sfloat, sampleCount,
    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
    vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal) );
    
	mMainCamera = mNode.make_child("Default Camera").make_component<Camera>(Camera::ProjectionMode::ePerspective, -1/1024.f, -numeric_limits<float>::infinity(), radians(70.f));
	mMainCamera.node().make_component<TransformData>(float3::Ones(), 1.f, make_quatf(0,0,0,1));
	
  OnUpdate.listen(mNode, [&](CommandBuffer& commandBuffer, float deltaTime) {
		if (mMainCamera) {
    	Window& window = commandBuffer.mDevice.mInstance.window();
			const MouseKeyboardState& input = window.input_state();
			auto cameraTransform = mMainCamera.node().find_in_ancestor<TransformData>();
			float fwd = (mMainCamera->mNear < 0) ? -1 : 1;
			if (!ImGui::GetIO().WantCaptureMouse) {
				if (input.pressed(KeyCode::eMouse2)) {
					static const float gMouseSensitivity = 0.002f;
					static float2 euler = float2::Zero();
					euler.y() += input.cursor_delta().x()*fwd * gMouseSensitivity;
					euler.x() = clamp(euler.x() + input.cursor_delta().y() * gMouseSensitivity, -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
					quatf rx = angle_axis(euler.x(), float3(fwd,0,0));
					quatf ry = angle_axis(euler.y(), float3(0,1,0));
					cameraTransform->mRotation = qmul(ry, rx);
				}
			}
			if (!ImGui::GetIO().WantCaptureKeyboard) {
				float3 mv = float3(0,0,0);
				if (input.pressed(KeyCode::eKeyD)) mv += float3( 1,0,0);
				if (input.pressed(KeyCode::eKeyA)) mv += float3(-1,0,0);
				if (input.pressed(KeyCode::eKeyW)) mv += float3(0,0, fwd);
				if (input.pressed(KeyCode::eKeyS)) mv += float3(0,0,-fwd);
				cameraTransform->mTranslation += rotate_vector(cameraTransform->mRotation, mv*deltaTime);
			}
		}
  });

  load_shaders();
}

void Application::load_shaders() {
  #pragma region load shaders
  #ifdef _WIN32
  wchar_t exepath[MAX_PATH];
  GetModuleFileNameW(NULL, exepath, MAX_PATH);
  #else
  char exepath[PATH_MAX];
  if (readlink("/proc/self/exe", exepath, PATH_MAX) == 0)
    ranges::uninitialized_fill(exepath, 0);
  #endif
  mNode.erase_component<ShaderDatabase>();
  ShaderModule::load_from_dir(*mNode.make_component<ShaderDatabase>(), mWindow.mInstance.device(), fs::path(exepath).parent_path()/"Shaders");
  #pragma endregion
}

void Application::loop() {
  vector<pair<Image::View, Image::View>> renderBuffers(mWindow.back_buffer_count());

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
    
    if (auto swapchainImage = mWindow.acquire_image(*commandBuffer)) {
      auto&[colorBuffer, depthBuffer] = renderBuffers[mWindow.back_buffer_index()];
      
      if (auto desc = mMainPass->find_attachment("colorBuffer"))
        if (!colorBuffer || colorBuffer.extent() != swapchainImage.extent() || colorBuffer.image()->usage() != mBackBufferUsage || colorBuffer.image()->sample_count() != desc->mDescription.samples) {
          ProfilerRegion ps("make colorBuffer attachment");
          colorBuffer = make_shared<Image>(commandBuffer->mDevice, "colorBuffer", swapchainImage.extent(), desc->mDescription, mBackBufferUsage);
        }
      if (auto desc = mMainPass->find_attachment("depthBuffer"))
        if (!depthBuffer || depthBuffer.extent() != swapchainImage.extent() || depthBuffer.image()->usage() != mDepthBufferUsage || swapchainImage.image()->sample_count() != desc->mDescription.samples) {
          ProfilerRegion ps("make depth attachment");
          depthBuffer = make_shared<Image>(commandBuffer->mDevice, "depthBuffer", swapchainImage.extent(), desc->mDescription, mDepthBufferUsage);
        }
      
      mMainPass->mPass.render(*commandBuffer, {
        { "colorBuffer", { colorBuffer, vk::ClearColorValue(std::array<float,4>{0,0,0,0}) } },
        { "depthBuffer", { depthBuffer, vk::ClearDepthStencilValue(0.f, 0) } }
      });

      if (colorBuffer.image()->sample_count() == vk::SampleCountFlagBits::e1)
        commandBuffer->copy_image(colorBuffer, swapchainImage);
      else
        commandBuffer->resolve_image(colorBuffer, swapchainImage);
      swapchainImage.transition_barrier(*commandBuffer, vk::ImageLayout::ePresentSrcKHR);

      pair<shared_ptr<Semaphore>, vk::PipelineStageFlags> waits(mWindow.image_available_semaphore(), vk::PipelineStageFlagBits::eAllCommands);
      auto semaphore = make_shared<Semaphore>(commandBuffer->mDevice, "RenderSemaphore");
      commandBuffer->mDevice.submit(commandBuffer, waits, semaphore);
      mWindow.present(**semaphore);
    }
  }
}