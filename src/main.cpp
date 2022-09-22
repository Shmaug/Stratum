#include "Node/Application.hpp"
#include "Node/Inspector.hpp"
#include "Node/XR.hpp"
#include "Node/FlyCamera.hpp"
#include "Node/ImageComparer.hpp"

#include "Node/BDPT.hpp"
#include "Node/VCM.hpp"

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

#ifdef STRATUM_ENABLE_OPENXR
component_ptr<XR> init_xr(Node& root_node, vector<string>& args) {
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
	return xrnode;
}
template<typename T>
void init_renderer(const auto& app, Node& renderer_node, const component_ptr<XR>& xrnode) {
	auto renderer = renderer_node.make_component<T>();
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
	return renderer;
};
#else
template<typename T>
component_ptr<T> init_renderer(const auto& app, Node& renderer_node, const component_ptr<Camera>& camera) {
	auto renderer = renderer_node.make_component<T>();
	app->OnRenderWindow.add_listener(renderer.node(), [=](CommandBuffer& commandBuffer) {
		renderer->render(commandBuffer, app->window().back_buffer(), { { camera->view(), node_to_world(camera.node()) } });
	});
	return renderer;
}
#endif

component_ptr<Camera> setup_camera(const auto& instance, const auto& scene) {
	Node& n = scene.node().make_child("Camera");
	// setup camera
	float3 pos = float3(0,1,0);
	if (auto p = instance->find_argument("cameraPosX"); p) pos[0] = (float)atof(p->c_str());
	if (auto p = instance->find_argument("cameraPosY"); p) pos[1] = (float)atof(p->c_str());
	if (auto p = instance->find_argument("cameraPosZ"); p) pos[2] = (float)atof(p->c_str());
	auto t  = n.make_component<TransformData>(make_transform(pos, quatf_identity(), float3::Ones()));

	auto fc = n.make_component<FlyCamera>();
	if (auto p = instance->find_argument("cameraRotX"); p) fc->mRotation[0] = (float)atof(p->c_str());
	if (auto p = instance->find_argument("cameraRotY"); p) fc->mRotation[1] = (float)atof(p->c_str());

	float fovy = radians(70.f);
	if (auto p = instance->find_argument("fovy"); p) fovy = (float)atof(p->c_str());
	if (auto p = instance->find_argument("fov"); p)  fovy = (float)atof(p->c_str());

	auto camera = n.make_component<Camera>(make_perspective(fovy, 1.f, float2::Zero(), -1 / 1024.f));

	return camera;
}

int main(int argc, char** argv) {
	cout << "Stratum " << STRATUM_VERSION_MAJOR << "." << STRATUM_VERSION_MINOR << endl;

	vector<string> args(argc);
	ranges::copy_n(argv, argc, args.begin());

	NodeGraph gNodeGraph;
	Node& root_node = gNodeGraph.emplace("Application");

	// init XR first, in order to load required extensions. extensions are added to the command line arguments
#ifdef STRATUM_ENABLE_OPENXR
	const auto xrnode = init_xr(root_node, args);
	if (!xrnode) { return 1; }
#endif

	// create vulkan instance and window
	auto instance = root_node.make_component<Instance>(args);

	vk::PhysicalDevice device;
#ifdef STRATUM_ENABLE_OPENXR
	device = xrnode->get_vulkan_device(*instance);
#endif
	instance->create_device();


	Node& app_node = root_node.make_child("Scene");

	const auto app       = app_node.make_component<Application>(instance->window());
	const auto gui       = app_node.make_component<Gui>();
	const auto inspector = app_node.make_component<Inspector>();
	const auto scene     = app_node.make_component<Scene>(); // scene hooks app/inspector events
#ifdef STRATUM_ENABLE_OPENXR
	xrnode->create_session(*instance);
#else
	const auto camera = setup_camera(instance, scene);
#endif

	// create renderer
	Node& renderer_node = app_node.make_child("Renderer");

	if (instance->find_argument("vcm")) {
#ifdef STRATUM_ENABLE_OPENXR
		init_renderer<VCM>(app, renderer_node, xrnode);
#else
		init_renderer<VCM>(app, renderer_node, camera);
#endif
	} else {
#ifdef STRATUM_ENABLE_OPENXR
		init_renderer<BDPT>(app, renderer_node, xrnode);
#else
		init_renderer<BDPT>(app, renderer_node, camera);
#endif
	}

	renderer_node.make_component<Denoiser>();
	renderer_node.make_component<ImageComparer>();

	for (const string& plugin_info : instance->find_arguments("plugin"))
		load_plugins(plugin_info, app.node());

	app->run();

	instance->device().flush();

	gNodeGraph.erase_recurse(root_node);

	return EXIT_SUCCESS;
}