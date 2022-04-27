#include "Node/Application.hpp"
#include "Node/Inspector.hpp"
#include "Node/PathTracer.hpp"
#include "Node/XR.hpp"

using namespace stm;

NodeGraph gNodeGraph;

void load_plugins(const string& plugin_info, Node& dst) {
	// load plugins
	size_t s0 = plugin_info.find(';');
	fs::path filename(plugin_info.substr(0, s0));
	auto plugin = dst.make_child(filename.stem().string()).make_component<dynamic_library>(filename);
	while (s0 != string::npos) {
		s0++;
		size_t s1 = plugin_info.find(';', s0);
		string fn = plugin_info.substr(s0, (s1 == string::npos) ? s1 : s1 - s0);
		cout << "Calling " << filename << ":" << fn << endl;
		plugin->invoke<void, Node&>(fn, ref(plugin.node()));
		s0 = s1;
	}
}

int main(int argc, char** argv) {
	cout << "Stratum " << STRATUM_VERSION_MAJOR << "." << STRATUM_VERSION_MINOR << endl;

	vector<string> args;
	for (int i = 0; i < argc; i++)
		args.emplace_back(argv[i]);

	Node& root_node = gNodeGraph.emplace("Application");

#ifdef STRATUM_ENABLE_OPENXR
	// init XR first, to load required extensions
	component_ptr<XR> xrnode;
	if (ranges::find_if(args, [](const string& s) { return s == "--xr"; }) != args.end()) {
		xrnode = root_node.make_child("XR").make_component<XR>();
		string instanceExtensions, deviceExtensions;
		xrnode->get_vulkan_extensions(instanceExtensions, deviceExtensions);

		string s;
		istringstream ss(instanceExtensions);
		while (getline(ss, s, ' ')) args.push_back("--instanceExtension:" + s);
		ss = istringstream(deviceExtensions);
		while (getline(ss, s, ' ')) args.push_back("--deviceExtension:" + s);
	}
#endif

	auto instance = root_node.make_component<Instance>(args);

	vk::PhysicalDevice device;
#ifdef STRATUM_ENABLE_OPENXR
	if (xrnode) device = xrnode->get_vulkan_device(*instance);
#endif
	instance->create_device();

	Node& app_node = root_node.make_child("Scene");
	auto app = app_node.make_component<Application>(instance->window());
	auto gui = app_node.make_component<Gui>();
	auto inspector = app_node.make_component<Inspector>();
	auto scene = app_node.make_component<Scene>();

#ifdef STRATUM_ENABLE_OPENXR
	if (xrnode) xrnode->create_session(*instance);
#endif

	Node& renderer_node = app_node.make_child("Renderer");
	auto renderer = renderer_node.make_component<PathTracer>();
	auto denoiser = renderer_node.make_component<Denoiser>();

	for (const string& plugin_info : instance->find_arguments("loadPlugin"))
		load_plugins(plugin_info, app.node());

	app->run();

	instance->device().flush();

	gNodeGraph.erase_recurse(root_node);

	return EXIT_SUCCESS;
}