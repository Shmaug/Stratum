#include "Window.hpp"
#include <Scene/Camera.hpp>

using namespace std;
using namespace stm;

Window::Window(Instance* instance, const string& title, MouseKeyboardInput* input, vk::Rect2D position
#ifdef WINDOWS
, HINSTANCE hInstance
#endif
#ifdef __linux
, xcb_connection_t* XCBConnection, xcb_screen_t* XCBScreen
#endif
	) : mInstance(instance), mTitle(title), mClientRect(position), mInput(input) {
	#ifdef WINDOWS
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
	if (!mHwnd) throw runtime_error("failed to create window");

	ShowWindow(mHwnd, SW_SHOW);
	
	vk::Win32SurfaceCreateInfoKHR info = {};
	info.hinstance = hInstance;
	info.hwnd = mHwnd;
	mSurface = (*mInstance)->createWin32SurfaceKHR(info);

	RECT cr;
	GetClientRect(mHwnd, &cr);
	mClientRect.offset = { (int32_t)cr.top, (int32_t)cr.left };
	mClientRect.extent = { (uint32_t)((int32_t)cr.right - (int32_t)cr.left), (uint32_t)((int32_t)cr.bottom - (int32_t)cr.top) };
	#endif
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
	#endif
}
Window::~Window() {
	DestroySwapchain();
	(*mInstance)->destroySurfaceKHR(mSurface);

	if (mDirectDisplay) {
		PFN_vkReleaseDisplayEXT vkReleaseDisplay = (PFN_vkReleaseDisplayEXT)(*mInstance)->getProcAddr("vkReleaseDisplayEXT");
		vkReleaseDisplay(mSwapchainDevice->PhysicalDevice(), mDirectDisplay);
	}
	
	#ifdef __linux
	if (mXCBConnection && mXCBWindow)
 		xcb_destroy_window(mXCBConnection, mXCBWindow);
 	#endif
	#ifdef WINDOWS
	::DestroyWindow(mHwnd);
	#endif
}

void Window::AcquireNextImage() {
	if (!mSwapchain && mSwapchainDevice) CreateSwapchain(mSwapchainDevice);
	if (!mSwapchain) return;

	mImageAvailableSemaphoreIndex = (mImageAvailableSemaphoreIndex + 1) % mImageAvailableSemaphores.size();
	auto result = (*mSwapchainDevice)->acquireNextImageKHR(mSwapchain, std::numeric_limits<uint64_t>::max(), **mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], nullptr);
	if (result.result == vk::Result::eErrorOutOfDateKHR || result.result == vk::Result::eSuboptimalKHR) {
		CreateSwapchain(mSwapchainDevice);
		if (!mSwapchain) return; // swapchain failed to create (happens when window is minimized, etc)
		result = (*mSwapchainDevice)->acquireNextImageKHR(mSwapchain, numeric_limits<uint64_t>::max(), **mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], nullptr);
	}
	mBackBufferIndex = result.value;
}

void Window::Present(const set<vk::Semaphore>& waitSemaphores) {
	if (!mSwapchain) return;

	vector<vk::Semaphore> semaphores(waitSemaphores.begin(), waitSemaphores.end());
	vector<vk::SwapchainKHR> swapchains { mSwapchain };
	vector<uint32_t> imageIndices { mBackBufferIndex };
	mSwapchainDevice->mQueueFamilies.at(mPresentQueueFamily).mQueue.presentKHR(vk::PresentInfoKHR(semaphores, swapchains, imageIndices));
	mSwapchainDevice->mFrameCount++;
}

#ifdef __linux
xcb_atom_t getReplyAtomFromCookie(xcb_connection_t* connection, xcb_intern_atom_cookie_t cookie) {
	xcb_generic_error_t * error;
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, &error);
	if (error) throw runtime_error("Could not set XCB screen: %d" + to_string(error->error_code));
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
	throw exception("window resize not implemented on linux");
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

void Window::CreateSwapchain(stm::Device* device) {
	if (mSwapchain) DestroySwapchain();
	mSwapchainDevice = device;
	mSwapchainDevice->SetObjectName(mSurface, "WindowSurface");
	
	#pragma region create swapchain
	vk::SurfaceCapabilitiesKHR capabilities = mSwapchainDevice->PhysicalDevice().getSurfaceCapabilitiesKHR(mSurface);
	vector<vk::SurfaceFormatKHR> formats = mSwapchainDevice->PhysicalDevice().getSurfaceFormatsKHR(mSurface);
	vector<vk::PresentModeKHR> presentModes = mSwapchainDevice->PhysicalDevice().getSurfacePresentModesKHR(mSurface);
	vector<vk::QueueFamilyProperties> queueFamilyProperties = mSwapchainDevice->PhysicalDevice().getQueueFamilyProperties();
	
	mPresentQueueFamily = -1;
	for (auto& [idx, queueFamily] : mSwapchainDevice->mQueueFamilies)
		if (mSwapchainDevice->PhysicalDevice().getSurfaceSupportKHR(idx, mSurface)) {
			mPresentQueueFamily = idx;
			break;
		}
	if (mPresentQueueFamily == -1) throw runtime_error("could not find a queue with surface support");

	// get the size of the swapchain
	mSwapchainExtent = capabilities.currentExtent;
	if (mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mSwapchainExtent.width > mSwapchainDevice->Limits().maxImageDimension2D || mSwapchainExtent.height > mSwapchainDevice->Limits().maxImageDimension2D)
		return; // invalid swapchain size, window probably minimized

	// select the format of the swapchain
	mSurfaceFormat = formats[0];
	for (const vk::SurfaceFormatKHR& format : formats)
		if (format.format == vk::Format::eB8G8R8A8Unorm)
			mSurfaceFormat = format;

	// select present mode: prefer mailbox always, then immediate if vsync is on, then fifo
	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
	for (const vk::PresentModeKHR& mode : presentModes)
		if (mode == vk::PresentModeKHR::eMailbox || (!mVSync && mode == vk::PresentModeKHR::eImmediate))
			presentMode = mode;

	vk::SwapchainCreateInfoKHR createInfo = {};
	createInfo.surface = mSurface;
	createInfo.minImageCount = capabilities.minImageCount + 1;
	createInfo.imageFormat = mSurfaceFormat.format;
	createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
	createInfo.imageExtent = mSwapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	createInfo.queueFamilyIndexCount = 1;
	createInfo.pQueueFamilyIndices = &mPresentQueueFamily;
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
	mSwapchain = (*mSwapchainDevice)->createSwapchainKHR(createInfo);
	mSwapchainDevice->SetObjectName(mSwapchain, "Window/Swapchain");
	#pragma endregion

	// get the back buffers
	vector<vk::Image> images = (*mSwapchainDevice)->getSwapchainImagesKHR(mSwapchain);
	mSwapchainImages.resize(images.size());
	mImageAvailableSemaphores.resize(images.size());
	mBackBufferIndex = 0;
	mImageAvailableSemaphoreIndex = 0;

	CommandBuffer* commandBuffer = device->GetCommandBuffer("Window Create", vk::QueueFlagBits::eTransfer);
	
	// create per-frame image views and semaphores
	for (uint32_t i = 0; i < mSwapchainImages.size(); i++) {
		mSwapchainImages[i].first = images[i];
		mSwapchainDevice->SetObjectName(mSwapchainImages[i].first, "Swapchain/Image" + to_string(i));

		commandBuffer->TransitionBarrier(mSwapchainImages[i].first, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);

		vk::ImageViewCreateInfo createInfo = {};
		createInfo.image = mSwapchainImages[i].first;
		createInfo.viewType = vk::ImageViewType::e2D;
		createInfo.format = mSurfaceFormat.format;
		createInfo.components.r = vk::ComponentSwizzle::eIdentity;
		createInfo.components.g = vk::ComponentSwizzle::eIdentity;
		createInfo.components.b = vk::ComponentSwizzle::eIdentity;
		createInfo.components.a = vk::ComponentSwizzle::eIdentity;
		createInfo.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		mSwapchainImages[i].second = (*mSwapchainDevice)->createImageView(createInfo);
		mSwapchainDevice->SetObjectName(mSwapchainImages[i].second, "Swapchain/View" + to_string(i));
		
		mImageAvailableSemaphores[i] = new Semaphore("Swapchain/ImageAvaiable", mSwapchainDevice);
	}
	
	device->Execute(commandBuffer);
}
void Window::DestroySwapchain() {
	mSwapchainDevice->Flush();

	for (uint32_t i = 0; i < mSwapchainImages.size(); i++) {
		if (mSwapchainImages[i].second) (*mSwapchainDevice)->destroyImageView(mSwapchainImages[i].second);
		safe_delete(mImageAvailableSemaphores[i]);
	}
	mSwapchainImages.clear();
	mPresentQueueFamily = -1;
	mImageAvailableSemaphores.clear();
	if (mSwapchain) (*mSwapchainDevice)->destroySwapchainKHR(mSwapchain);
	mSwapchain = nullptr;
}