#include <Core/Instance.hpp>
#include <Core/Device.hpp>
#include <Core/Window.hpp>
#include <Core/PluginManager.hpp>
#include <Input/InputManager.hpp>
#include <Util/Profiler.hpp>
#include <Util/Tokenizer.hpp>

using namespace std;

bool Instance::sDisableDebugCallback = false;
Instance* gInstance = nullptr;

// Debug messenger functions
#ifdef ENABLE_DEBUG_LAYERS
bool PrintValidationError(const char* msg) {
	char func[128];
	if (sscanf(msg, "Validation Error: [ %s ] ", func) == 0) return false;
	msg = strchr(msg, ']') + 2;

	uint32_t objectId;
	uint64_t objectHandle;
	char objectName[128];
	char objectType[128];
	struct VulkanObject {
		uint32_t mObjectId;
		uint64_t mObjectHandle;
		string mName;
		string mType;
	};
	vector<VulkanObject> objects;
	while (sscanf(msg, "Object %u: handle = %llx name = %s type = %s", &objectId, &objectHandle, objectName, objectType) == 4) {
		objectName[max(1u, (uint32_t)strlen(objectName)) - 1] = '\0';
		objectName[max(1u, (uint32_t)strlen(objectType)) - 1] = '\0';
		objects.push_back({ objectId, objectHandle, objectName, objectType });
		msg = strchr(msg, ';') + 2;
	}

	msg = strstr(msg, "| MessageID = ");
	if (!msg) return false;

	uint64_t messageId;
	if (sscanf(msg, "| MessageID = %llx | ", &messageId) == 0) return false;

	const char* lblStart = strstr(msg, "[");
	if (!lblStart) { fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", msg); return true; }
	lblStart++;

	while (lblStart) {
		string prev(msg, lblStart - msg);
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s", prev.c_str());

		const char* lblEnd = strstr(lblStart, "]");
		if (!lblEnd) { fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", lblStart); return true; }

		string lbl(lblStart, lblEnd - lblStart);
		fprintf_color(ConsoleColorBits::eCyan, stderr, "%s", lbl.c_str());

		msg = lblEnd;
		lblStart = strstr(msg, "[");
		lblStart++;
	}

	if (msg) fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", msg);
	return true;
}
VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	if (Instance::sDisableDebugCallback) return VK_FALSE;

	ConsoleColorBits c = ConsoleColorBits::eWhite;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
		throw pCallbackData->pMessage;
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		fprintf_color(ConsoleColorBits::eYellow, stderr, "%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
	else
		printf("%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);

	return VK_FALSE;
}
#endif

#ifdef WINDOWS
LRESULT CALLBACK Instance::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_PAINT:
		break;
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
		break;
	case WM_DESTROY:
	case WM_QUIT:
	case WM_MOVE:
	case WM_SIZE:
		gInstance->HandleMessage(hwnd, message, wParam, lParam);
		break;
	default:
		return DefWindowProcA(hwnd, message, wParam, lParam);
	}
	return 0;
}
#endif

Instance::Instance(int argc, char** argv) {
	gInstance = this;

	vector<XrExtensionProperties> xrExtensions;

	for (int i = 0; i < argc; i++)
		mCmdArguments.push_back(argv[i]);

	bool debugMessenger = true;
	uint32_t deviceIndex = 0;
	vk::Rect2D windowPosition = { { 160, 90 }, { 1600, 900 } };
	bool fullscreen = false;
	for (int i = 0; i < argc; i++) {
		if (mCmdArguments[i] == "--device") {
			if (++i < argc) deviceIndex = atoi(argv[i]);
		} else if (mCmdArguments[i] == "--fullscreen")
			fullscreen = true;
		else if (mCmdArguments[i] == "--width") {
			if (++i < argc) windowPosition.extent.width = atoi(argv[i]);
		}
		else if (mCmdArguments[i] == "--height") {
			if (++i < argc) windowPosition.extent.height = atoi(argv[i]);
		}
		else if (mCmdArguments[i] == "--nodebug")
			debugMessenger = false;
		else if (mCmdArguments[i] == "--xr") {
			uint32_t tmp;
			uint32_t xrExtensionCount;
			xrEnumerateInstanceExtensionProperties(nullptr, 0, &xrExtensionCount, nullptr);
			if (xrExtensionCount) {
				xrExtensions.resize(xrExtensionCount);
				for (uint32_t i = 0; i < xrExtensionCount; i++)
					xrExtensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
				xrEnumerateInstanceExtensionProperties(nullptr, xrExtensionCount, &tmp, xrExtensions.data());
				printf("Found %u OpenXR extensions.\n", xrExtensionCount);
			} else {
				printf_color(ConsoleColorBits::eYellow, "No OpenXR extensions found\n");
			}
		}
	}

	mPluginManager = new ::PluginManager();
	mInputManager = new ::InputManager();

	set<string> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	vector<const char*> validationLayers;
	#ifdef ENABLE_DEBUG_LAYERS
	mDebugMessenger = nullptr;
	instanceExtensions.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	validationLayers.push_back("VK_LAYER_KHRONOS_validation");
	#endif
	
	#ifdef __linux
	mInstanceExtensions.insert(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	mInstanceExtensions.insert(VK_KHR_DISPLAY_EXTENSION_NAME);
	#else
	instanceExtensions.insert(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	#endif

	mPluginManager->LoadPlugins();
	
	for (EnginePlugin* p : mPluginManager->Plugins()) {
		set<string> pluginExtensions = p->InstanceExtensionsRequired();
		for (const string& e : pluginExtensions) instanceExtensions.insert(e);
	}

	#pragma region Create Vulkan Instance
	if (validationLayers.size()) {
		vector<vk::LayerProperties> availableLayers = vk::enumerateInstanceLayerProperties();

		set<string> availableLayerSet;
		for (const vk::LayerProperties& layer : availableLayers)
			availableLayerSet.insert(layer.layerName);

		for (auto it = validationLayers.begin(); it != validationLayers.end();) {
			if (availableLayerSet.count(*it)) it++;
			else {
				fprintf_color(ConsoleColorBits::eYellow, stderr, "Removing unsupported layer: %s\n", *it);
				it = validationLayers.erase(it);
			}
		}
	}

	vk::ApplicationInfo appInfo = {};
	appInfo.pApplicationName = "StratumApplication";
	appInfo.applicationVersion = VK_MAKE_VERSION(0,0,0);
	appInfo.pEngineName = "Stratum";
	appInfo.engineVersion = VK_MAKE_VERSION(STRATUM_VERSION_MAJOR,STRATUM_VERSION_MINOR, 0);
	appInfo.apiVersion = VK_API_VERSION_1_2;

	vector<const char*> instanceExts;
	for (const string& s : instanceExtensions) instanceExts.push_back(s.c_str());

	vk::InstanceCreateInfo createInfo = {};
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = (uint32_t)instanceExts.size();
	createInfo.ppEnabledExtensionNames = instanceExts.data();
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	printf("Creating vulkan instance... ");
	mInstance = vk::createInstance(createInfo);
	printf_color(ConsoleColorBits::eGreen, "%s", "Done.\n");
	#pragma endregion

	#ifdef ENABLE_DEBUG_LAYERS
	if (debugMessenger) {
		#ifdef ENABLE_DEBUG_LAYERS
		auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT");
		#endif

		vk::DebugUtilsMessengerCreateInfoEXT msgr = {};
		msgr.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
		msgr.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
		msgr.pfnUserCallback = DebugCallback;
		printf("Creating debug messenger... ");
		vk::Result result = (vk::Result)vkCreateDebugUtilsMessengerEXT(mInstance, reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&msgr), nullptr, reinterpret_cast<VkDebugUtilsMessengerEXT*>(&mDebugMessenger));
		if (result == vk::Result::eSuccess)
			printf_color(ConsoleColorBits::eGreen, "%s", "Success.\n");
		else {
			printf_color(ConsoleColorBits::eRed, "%s", "Failed.\n");
			mDebugMessenger = nullptr;
		}
	}
	#endif

	vector<vk::PhysicalDevice> devices = mInstance.enumeratePhysicalDevices();
	if (devices.empty()) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Error: No Vulkan devices found\n");
		throw;
	}
	if (deviceIndex >= devices.size()) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "Warning: Device index out of bounds: %u. Defaulting to device 0\n", deviceIndex);
		deviceIndex = 0;
	}
	vk::PhysicalDevice physicalDevice = devices[deviceIndex];
	auto deviceProperties = physicalDevice.getProperties();

	printf("Using physical device index %u %s\nVulkan %u.%u.%u\n", deviceIndex, deviceProperties.deviceName.data(), VK_VERSION_MAJOR(deviceProperties.apiVersion), VK_VERSION_MINOR(deviceProperties.apiVersion), VK_VERSION_PATCH(deviceProperties.apiVersion));

	set<string> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME
	};

	for (EnginePlugin* p : mPluginManager->Plugins()) {
		set<string> pluginExtensions = p->DeviceExtensionsRequired(physicalDevice);
		for (const string& e : pluginExtensions) deviceExtensions.insert(e);
	}

	mMouseKeyboardInput = new MouseKeyboardInput();

	#ifdef __linux
	// create xcb connection
	mXCBConnection = xcb_connect(nullptr, nullptr);
	if (int err = xcb_connection_has_error(mXCBConnection)){
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to connect to xcb: %d\n", err);
		throw;
	}
	mXCBKeySymbols = xcb_key_symbols_alloc(mXCBConnection);

	// find xcb screen
	for (xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(mXCBConnection)); iter.rem; xcb_screen_next(&iter)) {
		xcb_screen_t* screen = iter.data;

		// find suitable physical device
		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		vector<vk::QueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

		for (uint32_t q = 0; q < queueFamilyCount; q++){
			if (vkGetPhysicalDeviceXcbPresentationSupportKHR(physicalDevice, q, mXCBConnection, screen->root_visual)){
				mWindow = new ::Window(this, applicationName, mMouseKeyboardInput, windowPosition, mXCBConnection, screen);
				break;
			}
		}
		if (mWindow) break;
	}
	if (!mWindow) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s", "Failed to find a device with XCB presentation support!\n");
		throw;
	}
	#else
	// Create window class
	HINSTANCE hInstance = GetModuleHandleA(NULL);

	WNDCLASSEXA windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEXA);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInstance;
	windowClass.hIcon = ::LoadIcon(hInstance, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = "Stratum";
	windowClass.hIconSm = ::LoadIcon(hInstance, NULL); //  MAKEINTRESOURCE(APPLICATION_ICON));
	HRESULT hr = ::RegisterClassExA(&windowClass);
	if (FAILED(hr)) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to register window class\n");
		throw;
	}
	// register raw input devices
	RAWINPUTDEVICE rID[2];
	// Mouse
	rID[0].usUsagePage = 0x01;
	rID[0].usUsage = 0x02;
	rID[0].dwFlags = 0;
	rID[0].hwndTarget = NULL;
	// Keyboard
	rID[1].usUsagePage = 0x01;
	rID[1].usUsage = 0x06;
	rID[1].dwFlags = 0;
	rID[1].hwndTarget = NULL;
	if (RegisterRawInputDevices(rID, 2, sizeof(RAWINPUTDEVICE)) == FALSE)
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to register raw input device(s)\n");
	mWindow = new ::Window(this, createInfo.pApplicationInfo->pApplicationName, mMouseKeyboardInput, windowPosition, hInstance);
	#endif
	mInputManager->RegisterInputDevice(mMouseKeyboardInput);
	
	if (fullscreen) mWindow->Fullscreen(true);

	uint32_t graphicsQueue, presentQueue;
	FindQueueFamilies(physicalDevice, mWindow->Surface(), graphicsQueue, presentQueue);

	mDevice = new ::Device(this, physicalDevice, deviceIndex, graphicsQueue, presentQueue, deviceExtensions, validationLayers);
	mWindow->CreateSwapchain(mDevice);
}
Instance::~Instance() {
	#ifdef __linux
	xcb_key_symbols_free(mXCBKeySymbols);
	xcb_disconnect(mXCBConnection);
	#else
	UnregisterClassA("Stratum", GetModuleHandleA(NULL));
	#endif

	safe_delete(mPluginManager);

	mInputManager->UnregisterInputDevice(mMouseKeyboardInput);
	safe_delete(mInputManager);
	safe_delete(mMouseKeyboardInput);

	safe_delete(mWindow);
	safe_delete(mDevice);

	#ifdef ENABLE_DEBUG_LAYERS
	if (mDebugMessenger) {
		auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT");
		vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
	}
	#endif

	vkDestroyInstance(mInstance, nullptr);

	#ifdef PROFILER_ENABLE
	Profiler::ClearAll();
	#endif
}


#ifdef WINDOWS
void Instance::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message){
	case WM_DESTROY:
	case WM_QUIT:
		mDestroyPending = true;
		break;
	case WM_SIZE:
	case WM_MOVE:
		if (mWindow && mWindow->mHwnd == hwnd){
			RECT cr;
			GetClientRect(mWindow->mHwnd, &cr);
			mWindow->mClientRect.offset = { (int32_t)cr.top, (int32_t)cr.left };
			mWindow->mClientRect.extent = { (uint32_t)((int32_t)cr.right - (int32_t)cr.left), (uint32_t)((int32_t)cr.bottom - (int32_t)cr.top) };
		}
		break;
	}
}
#elif defined(__linux)
void Instance::ProcessEvent(xcb_generic_event_t* event) {
	xcb_motion_notify_event_t* mn = (xcb_motion_notify_event_t*)event;
	xcb_resize_request_event_t* rr = (xcb_resize_request_event_t*)event;
	xcb_button_press_event_t* bp = (xcb_button_press_event_t*)event;
	xcb_key_press_event_t* kp = (xcb_key_press_event_t*)event;
	xcb_key_release_event_t* kr = (xcb_key_release_event_t*)event;
	xcb_client_message_event_t* cm = (xcb_client_message_event_t*)event;

	KeyCode kc;

	switch (event->response_type & ~0x80) {
	case XCB_MOTION_NOTIFY:
		if (mn->same_screen){
			mMouseKeyboardInput->mCurrent.mCursorPos = float2((float)mn->event_x, (float)mn->event_y);
			if (mWindow->mXCBWindow == mn->event)
				if (mWindow->mTargetCamera){
					float2 uv = mMouseKeyboardInput->mCurrent.mCursorPos / float2((float)mWindow->mClientRect.extent.width, (float)mWindow->mClientRect.extent.height);
					mMouseKeyboardInput->mMousePointer.mWorldRay = mWindow->mTargetCamera->ScreenToWorldRay(uv);
				}
		}
		break;

	case XCB_KEY_PRESS:
		kc = (KeyCode)xcb_key_press_lookup_keysym(mXCBKeySymbols, kp, 0);
		mMouseKeyboardInput->mCurrent.mKeys[kc] = true;
		if ((kc == KEY_LALT || kc == KEY_ENTER) && mMouseKeyboardInput->KeyDown(KEY_ENTER) && mMouseKeyboardInput->KeyDown(KEY_LALT))
			mWindow->Fullscreen(!mWindow->Fullscreen());
		break;
	case XCB_KEY_RELEASE:
		kc = (KeyCode)xcb_key_release_lookup_keysym(mXCBKeySymbols, kp, 0);
		mMouseKeyboardInput->mCurrent.mKeys[kc] = false;
		break;

	case XCB_BUTTON_PRESS:
		if (bp->detail == 4){
			mMouseKeyboardInput->mCurrent.mScrollDelta += 1.0f;
			break;
		}
		if (bp->detail == 5){
			mMouseKeyboardInput->mCurrent.mScrollDelta =- 1.0f;
			break;
		}
	case XCB_BUTTON_RELEASE:
		switch (bp->detail){
		case 1:
			mMouseKeyboardInput->mCurrent.mKeys[MOUSE_LEFT] = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
			mMouseKeyboardInput->mMousePointer.mPrimaryButton = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
			break;
		case 2:
			mMouseKeyboardInput->mCurrent.mKeys[MOUSE_MIDDLE] = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
			break;
		case 3:
			mMouseKeyboardInput->mCurrent.mKeys[MOUSE_RIGHT] = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
			mMouseKeyboardInput->mMousePointer.mSecondaryButton = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;
			break;
		}
		break;

	case XCB_CLIENT_MESSAGE:
		if (cm->data.data32[0] == mWindow->mXCBDeleteWin)
			mDestroyPending = true;
		break;
	}
}
xcb_generic_event_t* Instance::PollEvent() {
    xcb_generic_event_t* cur = xcb_poll_for_event(mXCBConnection);
	xcb_generic_event_t* nxt = xcb_poll_for_event(mXCBConnection);

	if (cur && (cur->response_type & ~0x80) == XCB_KEY_RELEASE &&
		nxt && (nxt->response_type & ~0x80) == XCB_KEY_PRESS) {

		xcb_key_press_event_t* kp = (xcb_key_press_event_t*)cur;
		xcb_key_press_event_t* nkp = (xcb_key_press_event_t*)nxt;

		if (nkp->time == kp->time && nkp->detail == kp->detail) {
			free(cur);
			free(nxt);
			return PollEvent(); // ignore repeat key press events
		}
	}

	if (cur) {
		ProcessEvent(cur);
		free(cur);
	}
	return nxt;
}
#endif

bool Instance::PollEvents() {
	#ifdef __linux
	xcb_generic_event_t* event;
	while (event = PollEvent()){
		if (!event) break;
		ProcessEvent(event);
		free(event);
	}

	xcb_get_geometry_cookie_t cookie = xcb_get_geometry(mXCBConnection, mWindow->mXCBWindow);
	if (xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(mXCBConnection, cookie, NULL)) {
		mWindow->mClientRect.offset.x = reply->x;
		mWindow->mClientRect.offset.y = reply->y;
		mWindow->mClientRect.extent.width = reply->width;
		mWindow->mClientRect.extent.height = reply->height;
		free(reply);
	}
	

	mMouseKeyboardInput->mCurrent.mCursorDelta = mMouseKeyboardInput->mCurrent.mCursorPos - mMouseKeyboardInput->mLast.mCursorPos;
	mMouseKeyboardInput->mWindowWidth = mWindow->mClientRect.extent.width;
	mMouseKeyboardInput->mWindowHeight = mWindow->mClientRect.extent.height;
	if (mDestroyPending) return false;

	#elif defined(WINDOWS)
	while (true) {
		if (mDestroyPending) return false;
		MSG msg = {};
		if (!GetMessageA(&msg, NULL, 0, 0)) return false;
		TranslateMessage(&msg);
		DispatchMessageA(&msg);

		if (mDestroyPending) return false;

		switch (msg.message) {
		case WM_INPUT: {
			uint32_t dwSize = 0;
			GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
			uint8_t* lpb = new uint8_t[dwSize];
			if (GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
				fprintf_color(ConsoleColorBits::eYellow, stderr, "Incorrect GetRawInputData size\n");
			RAWINPUT* raw = (RAWINPUT*)lpb;

			if (raw->header.dwType == RIM_TYPEMOUSE) {
				mMouseKeyboardInput->mCurrent.mCursorDelta += float2((float)raw->data.mouse.lLastX, (float)raw->data.mouse.lLastY);
				mMouseKeyboardInput->mMousePointer.mPrimaryAxis += raw->data.mouse.lLastX;
				mMouseKeyboardInput->mMousePointer.mSecondaryAxis += raw->data.mouse.lLastY;

				if (mMouseKeyboardInput->mLockMouse) {
					RECT rect;
					GetWindowRect(msg.hwnd, &rect);
					SetCursorPos((rect.right + rect.left) / 2, (rect.bottom + rect.top) / 2);
				}

				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) {
					mMouseKeyboardInput->mCurrent.mKeys[MOUSE_LEFT] = true;
					mMouseKeyboardInput->mMousePointer.mPrimaryButton = true;
				}
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP) {
					mMouseKeyboardInput->mCurrent.mKeys[MOUSE_LEFT] = false;
					mMouseKeyboardInput->mMousePointer.mPrimaryButton = false;
				}
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) {
					mMouseKeyboardInput->mCurrent.mKeys[MOUSE_RIGHT] = true;
					mMouseKeyboardInput->mMousePointer.mSecondaryButton = true;
				}
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP) {
					mMouseKeyboardInput->mCurrent.mKeys[MOUSE_RIGHT] = false;
					mMouseKeyboardInput->mMousePointer.mSecondaryButton = false;
				}
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) 	mMouseKeyboardInput->mCurrent.mKeys[MOUSE_MIDDLE] = true;
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP) 		mMouseKeyboardInput->mCurrent.mKeys[MOUSE_MIDDLE] = false;
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) 	mMouseKeyboardInput->mCurrent.mKeys[MOUSE_X1] = true;
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) 		mMouseKeyboardInput->mCurrent.mKeys[MOUSE_X1] = false;
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) 	mMouseKeyboardInput->mCurrent.mKeys[MOUSE_X2] = true;
				if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) 		mMouseKeyboardInput->mCurrent.mKeys[MOUSE_X2] = false;

				if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
					mMouseKeyboardInput->mCurrent.mScrollDelta += (short)(raw->data.mouse.usButtonData) / (float)WHEEL_DELTA;
					mMouseKeyboardInput->mMousePointer.mScrollDelta.x += (short)(raw->data.mouse.usButtonData) / (float)WHEEL_DELTA;
				}
			}
			if (raw->header.dwType == RIM_TYPEKEYBOARD) {
				USHORT key = raw->data.keyboard.VKey;
				if (key == VK_SHIFT) key = VK_LSHIFT;
				if (key == VK_MENU) key = VK_LMENU;
				if (key == VK_CONTROL) key = VK_LCONTROL;
				mMouseKeyboardInput->mCurrent.mKeys[(KeyCode)key] = (raw->data.keyboard.Flags & RI_KEY_BREAK) == 0;

				if ((raw->data.keyboard.Flags & RI_KEY_BREAK) == 0 &&
					((KeyCode)raw->data.keyboard.VKey == KEY_LALT || (KeyCode)raw->data.keyboard.VKey == KEY_ENTER) &&
					mMouseKeyboardInput->KeyDown(KEY_LALT) && mMouseKeyboardInput->KeyDown(KEY_ENTER)) {
					if (mWindow->mHwnd == msg.hwnd)
						mWindow->Fullscreen(!mWindow->Fullscreen());
				}
			}
			delete[] lpb;
			break;
		}
		case WM_SETFOCUS:
			printf("Got focus\n");
			break;
		case WM_KILLFOCUS:
			printf("Lost focus\n");
			break;
		}

		if (msg.message == WM_PAINT) break; // break and allow a frame to execute
	}

	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(mWindow->mHwnd, &pt);
	mMouseKeyboardInput->mCurrent.mCursorPos = float2((float)pt.x, (float)pt.y);
	mMouseKeyboardInput->mWindowWidth = mWindow->mClientRect.extent.width;
	mMouseKeyboardInput->mWindowHeight = mWindow->mClientRect.extent.height;
	#endif
	
	return true;
}

bool Instance::BeginFrame() {
	#ifdef PROFILER_ENABLE
	Profiler::BeginFrame(mDevice->FrameCount());
	#endif

	PROFILER_BEGIN("Poll Events");
	for (InputDevice* d : mInputManager->InputDevices()) d->NextFrame();
	if (!PollEvents()) { // Window was closed
		PROFILER_END;
		return false;
	}
	PROFILER_END;

	PROFILER_BEGIN("Acquire Image");
	mWindow->AcquireNextImage();
	PROFILER_END;
	
	// TODO: replace mScene->mCameras[0]
	//mMouseKeyboardInput->mMousePointer.mWorldRay = scene->mCameras[0]->ScreenToWorldRay(mMouseKeyboardInput->mCurrent.mCursorPos / float2((float)mMouseKeyboardInput->mWindowWidth, (float)mMouseKeyboardInput->mWindowHeight));	
	return true;
}
void Instance::EndFrame(const std::vector<vk::Semaphore>& waitSemaphores) {
	PROFILER_BEGIN("PrePresent");
	for (EnginePlugin* p : mPluginManager->Plugins()) p->PrePresent();
	PROFILER_END;

	mWindow->Present(waitSemaphores);

	mDevice->PurgePooledResources(8);

	#ifdef PROFILER_ENABLE
	Profiler::EndFrame();
	#endif
}