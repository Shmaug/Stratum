#include "Node/Application.hpp"
#include "Node/Gui.hpp"
#include "Node/RasterScene.hpp"
#include "Node/RayTraceScene.hpp"

#ifdef STRATUM_ENABLE_OPENXR
#include "Node/XRScene.hpp"
#endif

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

int main(int argc, char** argv) {
  vector<string> args;
  for (int i = 0; i < argc; i++)
    args.emplace_back(argv[i]);

  Node& root = gNodeGraph.emplace("Instance");

#ifdef STRATUM_ENABLE_OPENXR
  auto xrscene = root.make_component<XRScene>();
  string instanceExtensions, deviceExtensions;
  xrscene->get_vulkan_extensions(instanceExtensions, deviceExtensions);
  
  string s;
  istringstream ss(instanceExtensions);
  while (getline(ss, s, ' ')) args.push_back("--instanceExtension:" + s);
  ss = istringstream(deviceExtensions);
  while (getline(ss, s, ' ')) args.push_back("--deviceExtension:" + s);
#endif

  auto instance = root.make_component<Instance>(args);
  auto app = instance.node().make_child("Application").make_component<Application>(instance->window());
  auto gui = app.node().make_child("ImGui").make_component<Gui>();

  if (instance->device().ray_query_features().rayQuery)
    app.node().make_component<RayTraceScene>();
  else
    app.node().make_component<RasterScene>();
  
  for (const string& plugin_info : instance->find_arguments("loadPlugin"))
    load_plugins(plugin_info, app.node());

  app->loop();

  instance->device().flush();

	gNodeGraph.erase_recurse(root);
	return EXIT_SUCCESS;
}