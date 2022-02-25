#include "Node/Application.hpp"
#include "Node/Gui.hpp"
#include "Node/RayTraceScene.hpp"
#include "Node/XR.hpp"

using namespace stm;
using namespace stm::hlsl;

NodeGraph gNodeGraph;

void make_scene(const component_ptr<Application>& app) {
  auto scene = app.node().make_component<RayTraceScene>();
  app->OnUpdate.listen(scene.node(), bind(&RayTraceScene::update, scene.get(), std::placeholders::_1, std::placeholders::_2), EventPriority::eAlmostLast);

#ifdef STRATUM_ENABLE_OPENXR
  auto xrnode = app.node().find_in_descendants<XR>();
  if (xrnode) {
    /*
    xrnode->OnRender.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      vector<ViewData> views;
      views.reserve(xrnode->views().size());
      for (const XR::View& v : xrnode->views()) {
        ViewData& view = views.emplace_back();
        view.camera_to_world = node_to_world(v.mCamera.node());
        view.world_to_camera = view.camera_to_world.inverse();
        view.projection = v.mCamera->mProjection;
        view.image_min = { v.mCamera->mImageRect.offset.x, v.mCamera->mImageRect.offset.y };
        view.image_max = { v.mCamera->mImageRect.offset.x + v.mCamera->mImageRect.extent.width, v.mCamera->mImageRect.offset.y + v.mCamera->mImageRect.extent.height };
      }
      scene->render(commandBuffer, xrnode->back_buffer(), views);
      xrnode->back_buffer().transition_barrier(commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
    });

    app->OnRenderWindow.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      commandBuffer.blit_image(xrnode->back_buffer(), app->window().back_buffer());
    }, EventPriority::eAlmostFirst);
    */
  } else
#endif
  {
    app->OnRenderWindow.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      ViewData view;
      view.camera_to_world = node_to_world(app->mMainCamera.node());
      view.world_to_camera = view.camera_to_world.inverse();
      view.projection = app->mMainCamera->mProjection;
      view.image_min = { app->mMainCamera->mImageRect.offset.x, app->mMainCamera->mImageRect.offset.y };
      view.image_max = { app->mMainCamera->mImageRect.offset.x + app->mMainCamera->mImageRect.extent.width, app->mMainCamera->mImageRect.offset.y + app->mMainCamera->mImageRect.extent.height };
      scene->render(commandBuffer, app->window().back_buffer(), { view });
    });
  }
}

void make_gui(const component_ptr<Application>& app) {
  auto gui = app.node().make_child("ImGui").make_component<Gui>();
  app->OnRenderWindow.listen(gui.node(), [=](CommandBuffer& commandBuffer) {
    gui->render(commandBuffer, app->window().back_buffer());
  }, EventPriority::eLast);
  
}

void load_plugins(const string& plugin_info, Node& dst) {
  // load plugins
  size_t s0 = plugin_info.find(';');
  fs::path filename(plugin_info.substr(0,s0));
  auto plugin = dst.make_child(filename.stem().string()).make_component<dynamic_library>(filename);
  while (s0 != string::npos) {
    s0++;
    size_t s1 = plugin_info.find(';', s0);
    string fn = plugin_info.substr(s0, (s1 == string::npos) ? s1 : s1-s0);
    cout << "Calling " << filename << ":" << fn << endl;
    plugin->invoke<void, Node&>(fn, ref(plugin.node()));
    s0 = s1;
  }
}

int main(int argc, char** argv) {
  vector<string> args;
  for (int i = 0; i < argc; i++)
    args.emplace_back(argv[i]);

  Node& instance_node = gNodeGraph.emplace("Instance");
  Node& app_node = instance_node.make_child("Application");

#ifdef STRATUM_ENABLE_OPENXR
  // init XR first, to load required extensions
  component_ptr<XR> xrnode;
  if (ranges::find_if(args, [](const string& s) { return s == "--xr"; }) != args.end()) {
    xrnode = app_node.make_child("XR").make_component<XR>();
    string instanceExtensions, deviceExtensions;
    xrnode->get_vulkan_extensions(instanceExtensions, deviceExtensions);
    
    string s;
    istringstream ss(instanceExtensions);
    while (getline(ss, s, ' ')) args.push_back("--instanceExtension:" + s);
    ss = istringstream(deviceExtensions);
    while (getline(ss, s, ' ')) args.push_back("--deviceExtension:" + s);
  }
#endif

  auto instance = instance_node.make_component<Instance>(args);
#ifdef STRATUM_ENABLE_OPENXR
  if (xrnode)
    instance->create_device(xrnode->get_vulkan_device(*instance));
  else
#endif
    instance->create_device();

  auto app = app_node.make_component<Application>(instance->window());

  make_gui(app);

  app->PreFrame.listen(instance.node(), bind(&Instance::poll_events, instance.get()));

#ifdef STRATUM_ENABLE_OPENXR
  if (xrnode) {
    xrnode->create_session(*instance);
    app->PreFrame.listen(xrnode.node(), bind_front(&XR::poll_events, xrnode.get()));
    app->OnUpdate.listen(xrnode.node(), bind(&XR::render, xrnode.get(), std::placeholders::_1), EventPriority::eAlmostLast + 1024);
    app->PostFrame.listen(xrnode.node(), bind(&XR::present, xrnode.get()));
  }
#endif

  make_scene(app);

  for (const string& plugin_info : instance->find_arguments("loadPlugin"))
    load_plugins(plugin_info, app.node());

  app->run();

  instance->device().flush();

	gNodeGraph.erase_recurse(instance_node);
	return EXIT_SUCCESS;
}