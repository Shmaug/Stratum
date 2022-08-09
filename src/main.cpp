#include "Node/Application.hpp"
#include "Node/Inspector.hpp"
#include "Node/BDPT.hpp"
#include "Node/XR.hpp"
#include "Node/FlyCamera.hpp"

using namespace stm;

void load_plugins(const string& plugin_info, Node& dst) {
	// parse semicolin-delimited list of plugin libraries, turn them into dynamic_library nodes
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

	vector<string> args(argc);
	ranges::copy_n(argv, argc, args.begin());

	NodeGraph gNodeGraph;
	Node& root_node = gNodeGraph.emplace("Application");

	// init XR first, in order to load required extensions
#ifdef STRATUM_ENABLE_OPENXR
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
	const auto app       = app_node.make_component<Application>(instance->window());
	const auto gui       = app_node.make_component<Gui>();
	const auto inspector = app_node.make_component<Inspector>();
	const auto scene     = app_node.make_component<Scene>();
#ifdef STRATUM_ENABLE_OPENXR
	if (xrnode) xrnode->create_session(*instance);
#endif

	// create renderer
	Node& renderer_node = app_node.make_child("Renderer");
	const auto renderer = renderer_node.make_component<BDPT>();
	const auto denoiser = renderer_node.make_component<Denoiser>();

	for (const string& plugin_info : instance->find_arguments("plugin"))
		load_plugins(plugin_info, app.node());

	// setup viewer
#ifdef STRATUM_ENABLE_OPENXR
	if (xrnode) {
		xrnode->OnRender.add_listener(renderer.node(), [=](CommandBuffer& commandBuffer) {
			vector<pair<ViewData,TransformData>> views;
			views.reserve(xrnode->views().size());
			for (const XR::View& v : xrnode->views())
				views.emplace_back(v.mCamera.view(), node_to_world(v.mCamera.node()));
			renderer->render(commandBuffer, xrnode->back_buffer(), views);
			xrnode->back_buffer().transition_barrier(commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
		});
		app->OnRenderWindow.add_listener(renderer.node(), [=](CommandBuffer& commandBuffer) {
			commandBuffer.blit_image(xrnode->back_buffer(), app->window().back_buffer());
		}, Node::EventPriority::eAlmostFirst);
	} else
#endif
	{
		auto camera = scene.node().make_child("Camera").make_component<Camera>(make_perspective(radians(70.f), 1.f, float2::Zero(), -1 / 1024.f));
		camera.node().make_component<TransformData>(make_transform(float3(0, 1, 0), quatf_identity(), float3::Ones()));
		camera.node().make_component<FlyCamera>(camera.node());
		app->OnRenderWindow.add_listener(renderer.node(), [=](CommandBuffer& commandBuffer) {
			renderer->render(commandBuffer, app->window().back_buffer(), { { camera->view(), node_to_world(camera.node()) } });
		});
	}


	app->run();

	instance->device().flush();

	gNodeGraph.erase_recurse(root_node);

	return EXIT_SUCCESS;
}