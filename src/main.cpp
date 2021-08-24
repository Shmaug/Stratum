#include "Node/Application.hpp"
#include "Node/Gui.hpp"
#include "Node/RasterScene.hpp"

using namespace stm;
using namespace stm::hlsl;

NodeGraph gNodeGraph;

template<typename T> inline void type_gui_fn(T&) {}
template<> inline void type_gui_fn(Instance& instance) {
  ImGui::Text("Vulkan %u.%u.%u", 
    VK_API_VERSION_MAJOR(instance.vulkan_version()),
    VK_API_VERSION_MINOR(instance.vulkan_version()),
    VK_API_VERSION_PATCH(instance.vulkan_version()) );

  VmaStats stats;
  vmaCalculateStats(instance.device().allocator(), &stats);
  auto used = format_bytes(stats.total.usedBytes);
  auto unused = format_bytes(stats.total.usedBytes);
  ImGui::LabelText("Used memory", "%zu %s", used.first, used.second);
  ImGui::LabelText("Unused memory", "%zu %s", unused.first, unused.second);
  ImGui::LabelText("Device allocations", "%u", stats.total.blockCount);

  ImGui::LabelText("Window resolution", "%ux%u", instance.window().swapchain_extent().width, instance.window().swapchain_extent().height);
  ImGui::LabelText("Window format", to_string(instance.window().surface_format().format).c_str());
  ImGui::LabelText("Window color space", to_string(instance.window().surface_format().colorSpace).c_str());
}
template<> inline void type_gui_fn(spirv_module_map& spirv) {
  for (const auto&[name, spv] : spirv) {
    ImGui::LabelText(name.c_str(), "%s | %s", spv->entry_point().c_str(), to_string(spv->stage()).c_str());
  }
}
template<> inline void type_gui_fn(TransformData& t) {
  ImGui::DragFloat3("Translation", t.Translation.data(), .1f);
  ImGui::DragFloat("Scale", &t.Scale, .05f);
  if (ImGui::DragFloat4("Rotation (XYZW)", t.Rotation.xyz.data(), .1f, -1, 1))
    t.Rotation = normalize(t.Rotation);
}
template<> inline void type_gui_fn(LightData& light) {
  if (ImGui::BeginListBox("Light to world")) {
    type_gui_fn(light.mLightToWorld);
    ImGui::EndListBox();
  }

  ImGui::DragFloat3("Emission", light.mEmission.data());
  ImGui::DragFloat("Shadow bias", &light.mShadowBias, .1f, 0, 4);
  const char* items[] { "Distant", "Point", "Spot" };
  if (ImGui::BeginCombo("Type", items[light.mType])) {
    for (uint32_t i = 0; i < ranges::size(items); i++)
      if (ImGui::Selectable(items[i], light.mType==i))
        light.mType = i;
    ImGui::EndCombo();
  }
  ImGui::CheckboxFlags("Shadow map", &light.mFlags, LightFlags_Shadowmap);
}
template<> inline void type_gui_fn(DynamicRenderPass& rp) {
  for (auto& sp : rp.subpasses()) {
    if (ImGui::BeginListBox(sp->name().c_str())) {
      for (auto&[attachment, a] : sp->description().attachments()) {
          ImGui::Text(attachment.c_str());
          const char* items[] { "Input", "Color", "Resolve", "DepthStencil", "Preserve" };
          if (ImGui::BeginCombo("Type", items[a.mType])) {
            for (uint32_t i = 0; i < ranges::size(items); i++)
              if (ImGui::Selectable(items[i], a.mType==i))
                a.mType = (AttachmentType)i;
            ImGui::EndCombo();
          }
      }
      ImGui::EndListBox();
    }
  }
}
template<> inline void type_gui_fn(RasterScene::Camera& cam) {
  ImGui::CheckboxFlags("Perspective", &cam.mProjectionMode, ProjectionMode_Perspective);
  ImGui::CheckboxFlags("Right Handed", &cam.mProjectionMode, ProjectionMode_RightHanded);

  ImGui::DragFloat("Far", &cam.mFar);
  if (cam.mProjectionMode&ProjectionMode_Perspective)
    ImGui::DragFloat("Vertical FoV", &cam.mVerticalFoV, .1f, 0.00390625, numbers::pi_v<float>);
  else
    ImGui::DragFloat("Orthographic Height", &cam.mOrthographicHeight, .1f);
}

inline void components_gui_fn(Node& node) {
  auto component_gui_fn = []<typename T>(Node& node) {
    component_ptr<T> ptr = node.find<T>();
    if (ptr) {
      ImGui::Text(typeid(T).name());
      type_gui_fn(*ptr);
    }
  };
  component_gui_fn.operator()<Application>(node);
  component_gui_fn.operator()<Instance>(node);
  component_gui_fn.operator()<spirv_module_map>(node);
  component_gui_fn.operator()<RasterScene>(node);
  component_gui_fn.operator()<DynamicRenderPass>(node);
  component_gui_fn.operator()<Gui>(node);
  component_gui_fn.operator()<TransformData>(node);
  component_gui_fn.operator()<LightData>(node);
  component_gui_fn.operator()<RasterScene::Camera>(node);
  component_gui_fn.operator()<RasterScene::Submesh>(node);
}

inline void node_gui_fn(Node& n, Node*& selected) {
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (&n == selected) flags |= ImGuiTreeNodeFlags_Selected;
  if (n.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx(n.name().c_str(), flags)) {
    if (ImGui::IsItemClicked())
      selected = &n;
    for (Node& c : n.children()) {
      node_gui_fn(c, selected);
    }
    ImGui::TreePop();
  }
}

int main(int argc, char** argv) {
  Node& root = gNodeGraph.emplace("Instance");
	Instance& instance = *root.make_component<Instance>(argc, argv);
	
  #ifdef _WIN32
  wchar_t exepath[MAX_PATH];
  GetModuleFileNameW(NULL, exepath, MAX_PATH);
  #else
  char exepath[PATH_MAX];
  if (readlink("/proc/self/exe", exepath, PATH_MAX) == 0)
    ranges::uninitialized_fill(exepath, 0);
  #endif
  load_spirv_modules(*root.make_child("SPIR-V Modules").make_component<spirv_module_map>(), instance.device(), fs::path(exepath).parent_path()/"SPIR-V");

  auto scene = root.make_child("RasterScene").make_component<RasterScene>();
  auto app = root.make_child("Application").make_component<Application>(instance.window());
  auto& mainPass = *app->render_pass();
  
  scene.listen(mainPass.mPass.PreProcess, [&](CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
    scene->pre_render(commandBuffer, framebuffer, scene.node().find_in_descendants<RasterScene::Camera>());
  });
  scene.listen(mainPass.OnDraw, [&](CommandBuffer& commandBuffer) {
    scene->draw(commandBuffer);
  });
	

  auto imgui = app.node().make_child("ImGui").make_component<Gui>();

  Node* selected = nullptr;
  root.listen(imgui->OnGui, [&](CommandBuffer& commandBuffer) {

    Profiler::on_gui();

    if (ImGui::Begin("Scene Graph")) {
      ImGui::Columns(2);
      node_gui_fn(root, selected);
      ImGui::NextColumn();
      if (selected) {
        if (ImGui::BeginChild(selected->name().c_str()))
          components_gui_fn(*selected);
        ImGui::EndChild();
      }
    }
    ImGui::End();
  });

  imgui.listen(app->OnUpdate, [&](float deltaTime) { imgui->new_frame(instance.window(), deltaTime); });
  imgui.listen(mainPass.mPass.PreProcess, [&](CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
    imgui->render_gui(commandBuffer);
  }, EventPriority::eFirst + 100);
  imgui.listen(mainPass.OnDraw, [&](CommandBuffer& commandBuffer) {
    imgui->draw(commandBuffer);
  }, EventPriority::eLast);

  {
    auto commandBuffer = instance.device().get_command_buffer("Application::Application");
    
    imgui->create_textures(*commandBuffer);
    
    for (const string& envMap : instance.find_arguments("environmentMap")) {
      auto[pixels,extent] = Texture::load(instance.device(), envMap);
      Texture::View tex = make_shared<Texture>(instance.device(), "Environment Map", extent, pixels.format(), 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled);
      commandBuffer->upload_image(pixels.buffer(), tex);
      tex.texture()->generate_mip_maps(*commandBuffer);
      scene->set_skybox(tex.texture());
    }
    for (const string& filepath : instance.find_arguments("load_gltf"))
      scene->load_gltf(*commandBuffer, filepath);
    
    instance.device().submit(commandBuffer);
  }

  auto camera = scene.node().find_in_descendants<RasterScene::Camera>();
  auto cameraTransform = camera.node().find_in_ancestor<TransformData>();
	Vector2f euler = Vector2f::Zero();
  root.listen(app->OnUpdate, [&](float deltaTime) {
    Window& window = instance.window();

    if (!ImGui::GetIO().WantCaptureMouse) {
      if (window.pressed(KeyCode::eMouse2)) {
        euler += window.cursor_delta().reverse() * .0025f * (camera->mProjectionMode&ProjectionMode_RightHanded ? -1 : 1);
        euler.x() = clamp(euler.x(), -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
        quatf rx = angle_axis(euler.x(), float3(1,0,0));
        quatf ry = angle_axis(euler.y(), float3(0,1,0));
        cameraTransform->Rotation = qmul(rx, ry);
      }
    }
    if (!ImGui::GetIO().WantCaptureKeyboard) {
      float fwd = (camera->mProjectionMode&ProjectionMode_RightHanded) ? -1 : 1;
      float3 mv = float3(0,0,0);
      if (window.pressed(KeyCode::eKeyD)) mv += float3( 1,0,0);
      if (window.pressed(KeyCode::eKeyA)) mv += float3(-1,0,0);
      if (window.pressed(KeyCode::eKeyW)) mv += float3(0,0, fwd);
      if (window.pressed(KeyCode::eKeyS)) mv += float3(0,0,-fwd);
      cameraTransform->Translation += rotate_vector(cameraTransform->Rotation, mv*deltaTime);
    }
  });

  app->loop();

	gNodeGraph.erase_recurse(root);
	return EXIT_SUCCESS;
}