#include "Node/Application.hpp"
#include "Node/Gui.hpp"
#include "Node/RasterScene.hpp"
#include "Node/RayTraceScene.hpp"
#include "Node/XR.hpp"

using namespace stm;
using namespace stm::hlsl;

NodeGraph gNodeGraph;

inline void load_plugins(const string& plugin_info, Node& dst) {
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

void init_raster(const component_ptr<Application>& app) {
  auto scene = app.node().make_component<RasterScene>();
  app->OnUpdate.listen(scene.node(), bind(&RasterScene::update, scene.get(), std::placeholders::_1), EventPriority::eLast - 128);

  auto instance = app.node().find_in_ancestor<Instance>();

#ifdef STRATUM_ENABLE_OPENXR
  auto xrnode = app.node().find_in_descendants<XR>();
  if (xrnode) {
    xrnode->OnRender.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      for (const XR::View& v : xrnode->views())
        scene->render(commandBuffer, v.mCamera, xrnode->back_buffer(), v.mImageRect);
    });
    xrnode->back_buffer_usage() |= vk::ImageUsageFlagBits::eStorage;
  } else
#endif
  {
    app->OnRenderWindow.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      unordered_map<RenderAttachmentId, RenderPass::AttachmentInfo> attachments {
        { "colorBuffer", { AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription{ {},
            app->window().surface_format().format, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal } }
        },
        { "depthBuffer", { AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription{ {},
            vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal } }
        }
      };
      auto depthBuffer = make_shared<Image>(instance->device(), "Raster DepthStencil Buffer", );
      auto renderPass = make_shared<RenderPass>(instance->device(), "Raster RenderPass", attachments);
      auto framebuffer = make_shared<Framebuffer>(*renderPass, "Raster Framebuffer", vector<Image::View> { app->window().back_buffer(), depthBuffer });

      commandBuffer.begin_render_pass(renderPass, framebuffer, vk::Rect2D{ {}, framebuffer->extent() }, { vk::ClearValue(array<float,4>{0,0,0,0}), vk::ClearValue({ 0.f, 1.f }) });
      scene->render(commandBuffer, app->main_camera());
    });
  }
}

void init_rt(const component_ptr<Application>& app) {
  auto scene = app.node().make_component<RayTraceScene>();
  app->OnUpdate.listen(scene.node(), bind(&RayTraceScene::update, scene.get(), std::placeholders::_1), EventPriority::eLast - 128);

#ifdef STRATUM_ENABLE_OPENXR
  auto xrnode = app.node().find_in_descendants<XR>();
  if (xrnode) {
    xrnode->back_buffer_usage() |= vk::ImageUsageFlagBits::eStorage;
    xrnode->OnRender.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      for (const XR::View& v : xrnode->views())
        scene->render(commandBuffer, v.mCamera, xrnode->back_buffer(), v.mImageRect);
    });
  } else 
#endif
  {
    app->window().back_buffer_usage() |= vk::ImageUsageFlagBits::eStorage;
    app->OnRenderWindow.listen(scene.node(), [=](CommandBuffer& commandBuffer) {
      scene->render(commandBuffer, app->main_camera(), app->window().back_buffer());
    }, EventPriority::eFirst + 8);
  }
}

int main(int argc, char** argv) {
  vector<string> args;
  for (int i = 0; i < argc; i++)
    args.emplace_back(argv[i]);

  Node& root = gNodeGraph.emplace("Instance");

#ifdef STRATUM_ENABLE_OPENXR
  component_ptr<XR> xrnode;
  if (ranges::find_if(args, [](const string& s) { return s == "--xr"; }) != args.end()) {
    xrnode = root.make_component<XR>();
    string instanceExtensions, deviceExtensions;
    xrnode->get_vulkan_extensions(instanceExtensions, deviceExtensions);
    
    string s;
    istringstream ss(instanceExtensions);
    while (getline(ss, s, ' ')) args.push_back("--instanceExtension:" + s);
    ss = istringstream(deviceExtensions);
    while (getline(ss, s, ' ')) args.push_back("--deviceExtension:" + s);
  }
#endif

  auto instance = root.make_component<Instance>(args);
  auto app = root.make_child("Application").make_component<Application>(instance->window());
  
#ifdef STRATUM_ENABLE_OPENXR
  if (xrnode) {
    app->PreFrame.listen(root, bind_front(&XR::poll_actions, xrnode.get()));
    app->OnUpdate.listen(root, bind(&XR::do_frame, xrnode.get(), std::placeholders::_1), EventPriority::eLast - 128);
  }
#endif

  if (instance->device().ray_query_features().rayQuery)
    init_rt(app);
  else
    init_raster(app);

  for (const string& plugin_info : instance->find_arguments("loadPlugin"))
    load_plugins(plugin_info, app.node());

  app->loop();

  instance->device().flush();

	gNodeGraph.erase_recurse(root);
	return EXIT_SUCCESS;
}