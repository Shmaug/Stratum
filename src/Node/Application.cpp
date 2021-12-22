#include "Application.hpp"
#include "Gui.hpp"

using namespace stm;
using namespace stm::hlsl;

Application::Application(Node& node, Window& window) : mNode(node), mWindow(window) {
  node.make_child("ImGui").make_component<Gui>();

	mMainCamera = mNode.make_child("Default Camera").make_component<Camera>(Camera::ProjectionMode::ePerspective, -1/1024.f, -numeric_limits<float>::infinity(), radians(70.f));
	mMainCamera.node().make_component<TransformData>(make_transform(float3(0,1,0), make_quatf(0,0,0,1), float3::Ones()));
	
  if (!mNode.find_in_ancestor<Instance>()->find_argument("--xr"))
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
            quatf r = angle_axis(euler.x(), float3(fwd,0,0));
            r = qmul(angle_axis(euler.y(), float3(0,1,0)), r);
            #ifdef TRANSFORM_UNIFORM_SCALING
            cameraTransform->mRotation = r;
            #else
            cameraTransform->m.topLeftCorner(3,3) = Quaternionf(r.w, r.xyz[0], r.xyz[1], r.xyz[2]).matrix();
            #endif
          }
        }
        if (!ImGui::GetIO().WantCaptureKeyboard) {
          float3 mv = float3(0,0,0);
          if (input.pressed(KeyCode::eKeyD)) mv += float3( 1,0,0);
          if (input.pressed(KeyCode::eKeyA)) mv += float3(-1,0,0);
          if (input.pressed(KeyCode::eKeyW)) mv += float3(0,0, fwd);
          if (input.pressed(KeyCode::eKeyS)) mv += float3(0,0,-fwd);
          if (input.pressed(KeyCode::eKeySpace)) mv += float3(0,1,0);
          if (input.pressed(KeyCode::eKeyControl)) mv += float3(0,-1,0);
          if (input.pressed(KeyCode::eKeyShift)) mv *= 3;
          if (input.pressed(KeyCode::eKeyAlt)) mv /= 2;
          *cameraTransform = tmul(*cameraTransform, make_transform(mv*deltaTime, make_quatf(0,0,0,1), float3::Ones()));
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
  auto gui = mNode.find_in_descendants<Gui>();

  size_t frameCount = 0;
  auto t0 = chrono::high_resolution_clock::now();
  while (true) {
    ProfilerRegion ps("Frame " + to_string(frameCount++));

    mWindow.mInstance.poll_events();
    if (!mWindow.handle())
      break;

    {
      ProfilerRegion ps("Application::PreFrame");
      PreFrame();
    }

    auto t1 = chrono::high_resolution_clock::now();
    float deltaTime = chrono::duration_cast<chrono::duration<float>>(t1 - t0).count();
    t0 = t1;

    auto commandBuffer = mWindow.mInstance.device().get_command_buffer("Frame");
    
    {
      ProfilerRegion ps("Application::OnUpdate");
      OnUpdate(*commandBuffer, deltaTime);
    }
    
    if (mWindow.acquire_image(*commandBuffer)) {
      OnRenderWindow(*commandBuffer);
      
      gui->draw(*commandBuffer, mWindow.back_buffer());

      mWindow.back_buffer().transition_barrier(*commandBuffer, vk::ImageLayout::ePresentSrcKHR);

      auto renderFinishSemaphore = make_shared<Semaphore>(commandBuffer->mDevice, "RenderSemaphore");
      pair<shared_ptr<Semaphore>, vk::PipelineStageFlags> waits(mWindow.image_available_semaphore(), vk::PipelineStageFlagBits::eAllCommands);
      commandBuffer->mDevice.submit(commandBuffer, waits, renderFinishSemaphore);
      
      mWindow.present(**renderFinishSemaphore);
    }
  }
}