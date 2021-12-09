#include "Instance.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "Window.hpp"
#include "CommandBuffer.hpp"

#ifdef __linux
#include <xcb/xcb_util.h>
#endif

using namespace stm;

bool Instance::sDisableDebugCallback = false;
Instance* gInstance = nullptr;

// Debug messenger functions
VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	if (Instance::sDisableDebugCallback) return VK_FALSE;

	ConsoleColor c = ConsoleColor::eWhite;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		fprintf_color(ConsoleColor::eRed, stderr, "%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
		throw logic_error(pCallbackData->pMessage);
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		fprintf_color(ConsoleColor::eYellow, stderr, "%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
	else
		cout << pCallbackData->pMessageIdName << ": " << pCallbackData->pMessage << endl;

	return VK_FALSE;
}

#ifdef WIN32
LRESULT CALLBACK Instance::window_procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (gInstance && gInstance->mWindow)
			gInstance->mWindow->handle_message(message, wParam, lParam);
	switch (message) {
	default:
		return DefWindowProcA(hwnd, message, wParam, lParam);
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
		return 0;
	}
}
#endif

Instance::Instance(const vector<string>& args) : mCommandLine(args) {
	gInstance = this;

	#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
	VULKAN_HPP_DEFAULT_DISPATCHER.init(mDynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
	#endif

	for (const string& arg : mCommandLine) {
		size_t o = string::npos;
		if (arg.starts_with("--"))
			o = 2;
		else if (arg.starts_with("-") || arg.starts_with("/"))
			o = 1;
		if (o != string::npos) {
			size_t sep;
			if ((sep = arg.find('=')) != string::npos)
				mOptions.emplace(arg.substr(o,sep-o), arg.substr(sep+1));
			else if ((sep = arg.find(':')) != string::npos)
				mOptions.emplace(arg.substr(o,sep-o), arg.substr(sep+1));
			else
				mOptions.emplace(arg.substr(o), "");
		}
	}

	uint32_t deviceIndex = 0;
	if (auto index = find_argument("deviceIndex"))
		deviceIndex = stoi(*index);

	unordered_set<string> validationLayers;
	unordered_set<string> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
	unordered_set<string> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	
	for (const auto& layer : find_arguments("validationLayer")) validationLayers.emplace(layer);
	for (const auto& ext : find_arguments("instanceExtension")) instanceExtensions.emplace(ext);
	for (const auto& ext : find_arguments("deviceExtension")) deviceExtensions.emplace(ext);
	bool debugMessenger = find_argument("debugMessenger").has_value();
	
	if (debugMessenger) validationLayers.emplace("VK_LAYER_KHRONOS_validation");
	if (validationLayers.contains("VK_LAYER_KHRONOS_validation")) {
		instanceExtensions.emplace(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		instanceExtensions.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		instanceExtensions.emplace(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	if (deviceExtensions.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
		deviceExtensions.emplace(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		deviceExtensions.emplace(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
		deviceExtensions.emplace(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
	}
	if (deviceExtensions.contains(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
		deviceExtensions.emplace(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

	#ifdef WIN32
	instanceExtensions.emplace(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	#endif
	#ifdef __linux
	instanceExtensions.emplace(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	instanceExtensions.emplace(VK_KHR_DISPLAY_EXTENSION_NAME);
	#endif

	// remove unsupported layers
	if (validationLayers.size()) {
		unordered_set<string> available;
		for (const auto& layer : vk::enumerateInstanceLayerProperties()) available.emplace(layer.layerName.data());
		for (auto it = validationLayers.begin(); it != validationLayers.end();)
			if (available.find(*it) == available.end()) {
				fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Removing unsupported validation layer: %s\n", it->c_str());
				it = validationLayers.erase(it);
			} else 
				it++;
	}

	vector<const char*> layers;
	vector<const char*> instanceExts;
	vector<const char*> deviceExts;
	for (const string& s : validationLayers) layers.push_back(s.c_str());
	for (const string& s : instanceExtensions) instanceExts.push_back(s.c_str());
	for (const string& s : deviceExtensions) deviceExts.push_back(s.c_str());

	vk::ApplicationInfo appInfo = {};
	appInfo.pApplicationName = "StratumApplication";
	appInfo.applicationVersion = VK_MAKE_VERSION(0,0,0);
	appInfo.pEngineName = "Stratum";
	appInfo.engineVersion = VK_MAKE_VERSION(STRATUM_VERSION_MAJOR,STRATUM_VERSION_MINOR, 0);
	appInfo.apiVersion = mVulkanApiVersion = VK_API_VERSION_1_2;
	mInstance = vk::createInstance(vk::InstanceCreateInfo({}, &appInfo, layers, instanceExts));

	
	#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
  VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance);
	#endif

	if (debugMessenger) {
		cout << "Creating debug messenger...";
		mDebugMessenger = mInstance.createDebugUtilsMessengerEXT(vk::DebugUtilsMessengerCreateInfoEXT({},
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, 
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
			DebugCallback));
	}

	vector<vk::PhysicalDevice> devices = mInstance.enumeratePhysicalDevices();
	if (devices.empty()) throw runtime_error("no vulkan devices found");
	if (deviceIndex >= devices.size()) {
		fprintf_color(ConsoleColor::eYellow, stderr, "Warning: Device index out of bounds: %u. Defaulting to 0\n", deviceIndex);
		deviceIndex = 0;
	}
	vk::PhysicalDevice physicalDevice = devices[deviceIndex];
	auto deviceProperties = physicalDevice.getProperties();

	cout << "Using physical device " << deviceIndex << ": " << deviceProperties.deviceName << endl;
	cout << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "." << VK_VERSION_MINOR(deviceProperties.apiVersion) << "." << VK_VERSION_PATCH(deviceProperties.apiVersion) << endl;

	// Create window

	#ifdef WIN32
	// Create window class
	mHInstance = GetModuleHandleA(NULL);

	WNDCLASSEXA windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEXA);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &Instance::window_procedure;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = mHInstance;
	windowClass.hIcon = ::LoadIcon(mHInstance, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = "Stratum";
	windowClass.hIconSm = ::LoadIcon(mHInstance, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
	HRESULT hr = ::RegisterClassExA(&windowClass);
	if (FAILED(hr)) throw runtime_error("Failed to register window class");

	// register raw mMouseKeyboard devices
	vector<RAWINPUTDEVICE> rID(2);
	// Mouse
	rID[0].usUsagePage = 0x01;
	rID[0].usUsage = 0x02;
	// Keyboard
	rID[1].usUsagePage = 0x01;
	rID[1].usUsage = 0x06;
	if (RegisterRawInputDevices(rID.data(), (UINT)rID.size(), sizeof(RAWINPUTDEVICE)) == FALSE) throw runtime_error("Failed to register raw mMouseKeyboard device(s)");
	#endif

	#ifdef __linux
	// create xcb connection
	mXCBConnection = xcb_connect(":0", NULL);
	if (int err = xcb_connection_has_error(mXCBConnection)) throw runtime_error("Failed to connect to xcb: " + to_string(err));
	mXCBKeySymbols = xcb_key_symbols_alloc(mXCBConnection);

	// find xcb screen
	for (xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(mXCBConnection)); iter.rem; xcb_screen_next(&iter)) {
		xcb_screen_t* screen = iter.data;

		// find suitable physical device
		vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
		for (uint32_t q = 0; q < queueFamilyProperties.size(); q++)
			if (vkGetPhysicalDeviceXcbPresentationSupportKHR(physicalDevice, q, mXCBConnection, screen->root_visual)) {
				mXCBScreen = screen;
				break;
			}
		if (mXCBScreen) break;
	}
	if (!mXCBScreen) throw runtime_error("Failed to find a device with XCB presentation support!");
	#endif

	vk::Rect2D windowPosition = { { 160, 90 }, { 1600, 900 } };
	if (auto w = find_argument("width")) windowPosition.extent.width = stoi(*w);
	if (auto h = find_argument("height")) windowPosition.extent.height = stoi(*h);
	mWindow = make_unique<stm::Window>(*this, appInfo.pApplicationName, windowPosition);
	
	if (find_argument("fullscreen")) mWindow->fullscreen(true);
	
	mDevice = make_unique<stm::Device>(*this, physicalDevice, deviceExtensions, layers, mWindow->back_buffer_count());
	mWindow->create_swapchain(*mDevice);

	cout << mWindow->back_buffer_count() << " in-flight frames" << endl;
}
Instance::~Instance() {
	mWindow.reset();
	mDevice.reset();

	if (mDebugMessenger)
		mInstance.destroyDebugUtilsMessengerEXT(mDebugMessenger, nullptr);

	mInstance.destroy();

#ifdef WIN32
	UnregisterClassA("Stratum", GetModuleHandleA(NULL));
#elif defined(__linux)
	xcb_key_symbols_free(mXCBKeySymbols);
	xcb_disconnect(mXCBConnection);
#endif
}

void Instance::poll_events() const {
	ProfilerRegion ps("Instance::poll_events");
	mWindow->mInputStateLast = mWindow->mInputState;
	mWindow->mInputState.clear();

	#ifdef WIN32
	MSG msg = {};
	while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	#endif

	#ifdef __linux
	xcb_generic_event_t* event;
	while (event = mWindow->poll_event()) {
		if (!event) break;
		mWindow->process_event(event);
		free(event);
	}

	if (xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(mXCBConnection, xcb_get_geometry(mXCBConnection, mWindow->handle()), NULL)) {
		mWindow->mClientRect.offset.x = reply->x;
		mWindow->mClientRect.offset.y = reply->y;
		mWindow->mClientRect.extent.width = reply->width;
		mWindow->mClientRect.extent.height = reply->height;
		free(reply);
	}

	mWindow->mInputState.add_cursor_delta(mWindow->mInputState.cursor_pos() - mWindow->mInputStateLast.cursor_pos());
	#endif
}