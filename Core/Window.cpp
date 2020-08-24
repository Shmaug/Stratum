#include <Data/Texture.hpp>
#include <Core/Window.hpp>
#include <Core/Device.hpp>
#include <Core/Instance.hpp>
#include <Scene/Camera.hpp>
#include <Util/Profiler.hpp>
#include <Util/Util.hpp>

using namespace std;

Window::Window(Instance* instance, const string& title, MouseKeyboardInput* input, vk::Rect2D position
#ifdef __linux
, xcb_connection_t* XCBConnection, xcb_screen_t* XCBScreen
#else
, HINSTANCE hInstance
#endif
	) : mInstance(instance), mTitle(title), mClientRect(position), mInput(input) {
	#ifdef __linux
	mXCBConnection = XCBConnection;
	mXCBScreen = XCBScreen;
	mXCBWindow = xcb_generate_id(mXCBConnection);

	uint32_t valueList[] {
		XCB_EVENT_MASK_BUTTON_PRESS		| XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS   		| XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION	| XCB_EVENT_MASK_BUTTON_MOTION };
	xcb_create_window(
		mXCBConnection,
		XCB_COPY_FROM_PARENT,
		mXCBWindow,
		mXCBScreen->root,
		position.offset.x,
		position.offset.y,
		position.extent.width,
		position.extent.height,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		mXCBScreen->root_visual,
		XCB_CW_EVENT_MASK, valueList);

	xcb_change_property(
		mXCBConnection,
		XCB_PROP_MODE_REPLACE,
		mXCBWindow,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length(),
		title.c_str());

	xcb_intern_atom_cookie_t wmDeleteCookie = xcb_intern_atom(mXCBConnection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
	xcb_intern_atom_cookie_t wmProtocolsCookie = xcb_intern_atom(mXCBConnection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
	xcb_intern_atom_reply_t* wmDeleteReply = xcb_intern_atom_reply(mXCBConnection, wmDeleteCookie, NULL);
	xcb_intern_atom_reply_t* wmProtocolsReply = xcb_intern_atom_reply(mXCBConnection, wmProtocolsCookie, NULL);
	mXCBDeleteWin = wmDeleteReply->atom;
	mXCBProtocols = wmProtocolsReply->atom;
	xcb_change_property(mXCBConnection, XCB_PROP_MODE_REPLACE, mXCBWindow, wmProtocolsReply->atom, 4, 32, 1, &wmDeleteReply->atom);

	xcb_map_window(mXCBConnection, mXCBWindow);
	xcb_flush(mXCBConnection);

	VkXcbSurfaceCreateInfoKHR info = {};
	info.connection = mXCBConnection;
	info.window = mXCBWindow;
	mSurface = ((vk::Instance)*mInstance).vkCreateXcbSurfaceKHR(*mInstance, &info, nullptr, &mSurface);

	#else

	mWindowedRect = {};

	SetProcessDPIAware();

	mHwnd = CreateWindowExA(
		NULL,
		"Stratum",
		mTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		position.offset.x,
		position.offset.y,
		position.extent.width,
		position.extent.height,
		NULL,
		NULL,
		hInstance,
		nullptr );
	if (!mHwnd) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to create window\n");
		throw;
	}

	ShowWindow(mHwnd, SW_SHOW);
	
	vk::Win32SurfaceCreateInfoKHR info = {};
	info.hinstance = hInstance;
	info.hwnd = mHwnd;
	mSurface = ((vk::Instance)*mInstance).createWin32SurfaceKHR(info);

	RECT cr;
	GetClientRect(mHwnd, &cr);
	mClientRect.offset = { (int32_t)cr.top, (int32_t)cr.left };
	mClientRect.extent = { (uint32_t)((int32_t)cr.right - (int32_t)cr.left), (uint32_t)((int32_t)cr.bottom - (int32_t)cr.top) };
	#endif
}
Window::~Window() {
	DestroySwapchain();
	((vk::Instance)*mInstance).destroy(mSurface);

	if (mDirectDisplay) {
		PFN_vkReleaseDisplayEXT vkReleaseDisplay = (PFN_vkReleaseDisplayEXT)vkGetInstanceProcAddr((vk::Instance)*mInstance, "vkReleaseDisplayEXT");
		vkReleaseDisplay(mPhysicalDevice, mDirectDisplay);
	}
	
	#ifdef __linux
	if (mXCBConnection && mXCBWindow)
 		xcb_destroy_window(mXCBConnection, mXCBWindow);
 	#else
	DestroyWindow(mHwnd);
	#endif
}

vk::Image Window::AcquireNextImage() {
	if (!mSwapchain) return nullptr;

	mImageAvailableSemaphoreIndex = (mImageAvailableSemaphoreIndex + 1) % mImageAvailableSemaphores.size();
	auto result = ((vk::Device)*mDevice).acquireNextImageKHR(mSwapchain, std::numeric_limits<uint64_t>::max(), *mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], nullptr);
	if (result.result == vk::Result::eErrorOutOfDateKHR || result.result == vk::Result::eSuboptimalKHR) {
		CreateSwapchain(mDevice);
		if (!mSwapchain) return nullptr; // swapchain failed to create (happens when window is minimized, etc)
		result = ((vk::Device)*mDevice).acquireNextImageKHR(mSwapchain, numeric_limits<uint64_t>::max(), *mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], nullptr);
	}
	mBackBufferIndex = result.value;
	return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].first;
}

void Window::Present(const vector<vk::Semaphore>& waitSemaphores) {
	if (!mSwapchain) return;

	vk::PresentInfoKHR presentInfo = {};
	presentInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
	presentInfo.pWaitSemaphores = waitSemaphores.data();
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &mSwapchain;
	presentInfo.pImageIndices = &mBackBufferIndex;
	mDevice->PresentQueue().presentKHR(presentInfo);
	mDevice->mFrameCount++;
}

#ifdef __linux
xcb_atom_t getReplyAtomFromCookie(xcb_connection_t* connection, xcb_intern_atom_cookie_t cookie) {
	xcb_generic_error_t * error;
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, &error);
	if (error) {
		printf("Can't set the screen. Error Code: %d", error->error_code);
		throw;
	}
	return reply->atom;
}
#endif

void Window::Resize(uint32_t w, uint32_t h) {
	#ifdef WINDOWS
	RECT r;
	GetWindowRect(mHwnd, &r);
	int x = r.left, y = r.top;
	GetClientRect(mHwnd, &r);
	r.right = r.left + w;
	r.bottom = r.top + h;
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	SetWindowPos(mHwnd, HWND_TOPMOST, x, y, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
	#else
	fprintf_color(ConsoleColorBits::eRed, stderr, "Error: Window resize not impleneted on linux\n");
	#endif
}

void Window::Fullscreen(bool fs) {
	#ifdef WINDOWS
	if (fs && !mFullscreen) {
		GetWindowRect(mHwnd, &mWindowedRect);

		UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
		SetWindowLongW(mHwnd, GWL_STYLE, windowStyle);

		HMONITOR hMonitor = MonitorFromWindow(mHwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFOEX monitorInfo = {};
		monitorInfo.cbSize = sizeof(MONITORINFOEX);
		GetMonitorInfoA(hMonitor, &monitorInfo);

		SetWindowPos(mHwnd, HWND_TOPMOST,
			monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.top,
			monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);

		ShowWindow(mHwnd, SW_MAXIMIZE);

		mFullscreen = true;
	} else if (!fs && mFullscreen) {
		SetWindowLongA(mHwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
		SetWindowPos(mHwnd, HWND_NOTOPMOST,
			mWindowedRect.left,
			mWindowedRect.top,
			mWindowedRect.right  - mWindowedRect.left,
			mWindowedRect.bottom - mWindowedRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);
		ShowWindow(mHwnd, SW_NORMAL);

		mFullscreen = false;
	}
	#else

	if (fs == mFullscreen) return;
	mFullscreen = fs;

   	struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } hints = {0};

    //hints.flags = MWM_HINTS_DECORATIONS;
    //hints.decorations = mFullscreen ? 0 : MWM_DECOR_ALL;

	//xcb_intern_atom_cookie_t cookie = xcb_intern_atom(mXCBConnection, 0, 16, "_MOTIF_WM_HINTS");
	//xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(mXCBConnection, cookie, NULL);
	//xcb_change_property(mXCBConnection, XCB_PROP_MODE_REPLACE, mXCBWindow, reply->atom, reply->atom, 32, sizeof(hints), &hints);

	#endif
}

void Window::CreateSwapchain(::Device* device) {
	if (mSwapchain) DestroySwapchain();
	mDevice = device;
	mDevice->SetObjectName(mSurface, "WindowSurface");
	if (!mPhysicalDevice) mPhysicalDevice = mDevice->PhysicalDevice();
	
	#pragma region create swapchain
	// query support

	vk::SurfaceCapabilitiesKHR capabilities = mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface);
	vector<vk::SurfaceFormatKHR> formats = mPhysicalDevice.getSurfaceFormatsKHR(mSurface);
	vector<vk::PresentModeKHR> presentModes = mPhysicalDevice.getSurfacePresentModesKHR(mSurface);

	uint32_t graphicsFamily, presentFamily;
	if (!FindQueueFamilies(mPhysicalDevice, mSurface, graphicsFamily, presentFamily)) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s", "Error: Failed to find queue families\n");
		throw;
	}
	if (!mPhysicalDevice.getSurfaceSupportKHR(presentFamily, mSurface)) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s", "Error: Surface not supported by device!");
		throw;
	}

	uint32_t queueFamilyIndices[] = { graphicsFamily, presentFamily };

	// get the size of the swapchain
	mSwapchainExtent = capabilities.currentExtent;
	if (mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mSwapchainExtent.width > mDevice->Limits().maxImageDimension2D || mSwapchainExtent.height > mDevice->Limits().maxImageDimension2D)
		return;

	mFormat = formats[0];
	for (const vk::SurfaceFormatKHR& format : formats) {
		if (format.format == vk::Format::eB8G8R8A8Unorm)
			mFormat = format;
	}

	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
	for (const vk::PresentModeKHR& mode : presentModes) {
		if (mode == vk::PresentModeKHR::eMailbox ||
		   (!mVSync && mode == vk::PresentModeKHR::eImmediate))
			presentMode = mode;
	}

	vk::SwapchainCreateInfoKHR createInfo = {};
	createInfo.surface = mSurface;
	createInfo.minImageCount = capabilities.minImageCount + 1;
	createInfo.imageFormat = mFormat.format;
	createInfo.imageColorSpace = mFormat.colorSpace;
	createInfo.imageExtent = mSwapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	if (graphicsFamily != presentFamily) {
		createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else
		createInfo.imageSharingMode = vk::SharingMode::eExclusive;
	createInfo.preTransform = capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity ? vk::SurfaceTransformFlagBitsKHR::eIdentity : capabilities.currentTransform;
	createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	std::vector<vk::CompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
		vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
		vk::CompositeAlphaFlagBitsKHR::eInherit
	};
	for (auto& compositeAlphaFlag : compositeAlphaFlags) {
		if (capabilities.supportedCompositeAlpha & compositeAlphaFlag) {
			createInfo.compositeAlpha = compositeAlphaFlag;
			break;
		};
	}
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	mSwapchain = ((vk::Device)*mDevice).createSwapchainKHR(createInfo);
	mDevice->SetObjectName(mSwapchain, "WindowSwapchain");
	#pragma endregion

	// get the back buffers
	vector<vk::Image> images = ((vk::Device)*mDevice).getSwapchainImagesKHR(mSwapchain);
	mSwapchainImages.resize(images.size());
	mImageAvailableSemaphores.resize(images.size());
	mBackBufferIndex = 0;
	mImageAvailableSemaphoreIndex = 0;

	CommandBuffer* commandBuffer = device->GetCommandBuffer();
	
	// create per-frame image views and semaphores
	for (uint32_t i = 0; i < mSwapchainImages.size(); i++) {
		mSwapchainImages[i].first = images[i];
		mDevice->SetObjectName(mSwapchainImages[i].first, "SwapchainImage" + to_string(i));

		commandBuffer->TransitionBarrier(mSwapchainImages[i].first, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);

		vk::ImageViewCreateInfo createInfo = {};
		createInfo.image = mSwapchainImages[i].first;
		createInfo.viewType = vk::ImageViewType::e2D;
		createInfo.format = mFormat.format;
		createInfo.components.r = vk::ComponentSwizzle::eIdentity;
		createInfo.components.g = vk::ComponentSwizzle::eIdentity;
		createInfo.components.b = vk::ComponentSwizzle::eIdentity;
		createInfo.components.a = vk::ComponentSwizzle::eIdentity;
		createInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		mSwapchainImages[i].second = ((vk::Device)*mDevice).createImageView(createInfo);
		mDevice->SetObjectName(mSwapchainImages[i].second, "SwapchainView" + to_string(i));
		
		mImageAvailableSemaphores[i] = new Semaphore(mDevice);
		mDevice->SetObjectName(mImageAvailableSemaphores[i]->operator vk::Semaphore(), "SwapchainImageAvaiableSemaphore" + to_string(i));
	}
	
	device->Execute(commandBuffer);
	commandBuffer->Wait();
}

void Window::DestroySwapchain() {
	mDevice->Flush();

	for (uint32_t i = 0; i < mSwapchainImages.size(); i++) {
		if (mSwapchainImages[i].second) mDevice->Destroy(mSwapchainImages[i].second);
		mSwapchainImages[i].second = nullptr;
		delete mImageAvailableSemaphores[i];
	}
	mImageAvailableSemaphores.clear();

	if (mSwapchain) mDevice->Destroy(mSwapchain);
	mSwapchainImages.clear();
	mSwapchain = nullptr;
}