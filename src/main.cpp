#include "Node/Application.hpp"
#include "Node/RasterScene.hpp"
#include "Node/Gui.hpp"

using namespace stm;
using namespace stm::hlsl;

NodeGraph gNodeGraph;

int main(int argc, char** argv) {
  Node& root = gNodeGraph.emplace("Instance");
	Instance& instance = *root.make_component<Instance>(argc, argv);
	
  #pragma region load spirv files
  #ifdef _WIN32
  wchar_t exepath[MAX_PATH];
  GetModuleFileNameW(NULL, exepath, MAX_PATH);
  #else
  char exepath[PATH_MAX];
  if (readlink("/proc/self/exe", exepath, PATH_MAX) == 0)
    ranges::uninitialized_fill(exepath, 0);
  #endif
  load_spirv_modules(*root.make_child("SPIR-V Modules").make_component<spirv_module_map>(), instance.device(), fs::path(exepath).parent_path()/"SPIR-V");
  #pragma endregion

  auto app = root.make_child("Application").make_component<Application>(instance.window());
  auto gui = app.node().make_child("ImGui").make_component<Gui>();
  auto scene = app.node().make_child("RasterScene").make_component<RasterScene>();

  // load plugins
  for (const string& plugin_info : instance.find_arguments("load_plugin")) {
    size_t s0 = plugin_info.find(';');
    fs::path pluginFile(plugin_info.substr(0,s0));
    auto plugin = scene.node().make_child(pluginFile.stem().string()).make_component<dynamic_library>(pluginFile);
    while (s0 != string::npos) {
      s0++;
      size_t s1 = plugin_info.find(';', s0);
      string fn = plugin_info.substr(s0, (s1 == string::npos) ? s1 : s1-s0);
      cout << "Calling " << pluginFile << ":" << fn << endl;
      plugin->invoke<void, Node&>(fn, ref(plugin.node()));
      s0 = s1;
    }
  }

  app->loop();

	gNodeGraph.erase_recurse(root);
	return EXIT_SUCCESS;
}