#include "Window.hpp"

using namespace stm;

stm::Window::Window(Instance& instance, const string& title, vk::Rect2D position) : mInstance(instance), mTitle(title), mClientRect(position) {
	#ifdef WIN32

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
		mInstance.hInstance(),
		nullptr );
	if (!mHwnd) throw runtime_error("failed to create window");

	ShowWindow(mHwnd, SW_SHOW);
	
	vk::Win32SurfaceCreateInfoKHR info = {};
	info.hinstance = mInstance.hInstance();
	info.hwnd = mHwnd;
	mSurface = mInstance->createWin32SurfaceKHR(info);

	RECT cr;
	GetClientRect(mHwnd, &cr);
	mClientRect.offset = vk::Offset2D((int32_t)cr.top, (int32_t)cr.left);
	mClientRect.extent = vk::Extent2D((uint32_t)(cr.right - cr.left), (uint32_t)(cr.bottom - cr.top));
	#endif

	#ifdef __linux
	xcb_connection_t* xcbConnection = mInstance.xcb_connection();
	xcb_screen_t* xcbScreen = mInstance.xcb_screen();
	mXCBWindow = xcb_generate_id(xcbConnection);

	uint32_t valueList[] {
		XCB_EVENT_MASK_BUTTON_PRESS		| XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS   		| XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION	| XCB_EVENT_MASK_BUTTON_MOTION };
	xcb_create_window(
		xcbConnection,
		XCB_COPY_FROM_PARENT,
		mXCBWindow,
		xcbScreen->root,
		position.offset.x,
		position.offset.y,
		position.extent.width,
		position.extent.height,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		xcbScreen->root_visual,
		XCB_CW_EVENT_MASK, valueList);

	xcb_change_property(
		xcbConnection,
		XCB_PROP_MODE_REPLACE,
		mXCBWindow,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length(),
		title.c_str());

	xcb_intern_atom_cookie_t wmDeleteCookie = xcb_intern_atom(xcbConnection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
	xcb_intern_atom_cookie_t wmProtocolsCookie = xcb_intern_atom(xcbConnection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
	xcb_intern_atom_reply_t* wmDeleteReply = xcb_intern_atom_reply(xcbConnection, wmDeleteCookie, NULL);
	xcb_intern_atom_reply_t* wmProtocolsReply = xcb_intern_atom_reply(xcbConnection, wmProtocolsCookie, NULL);
	mXCBDeleteWin = wmDeleteReply->atom;
	mXCBProtocols = wmProtocolsReply->atom;
	xcb_change_property(xcbConnection, XCB_PROP_MODE_REPLACE, mXCBWindow, wmProtocolsReply->atom, 4, 32, 1, &wmDeleteReply->atom);

	xcb_map_window(xcbConnection, mXCBWindow);
	xcb_flush(xcbConnection);

	mSurface = instance->createXcbSurfaceKHR(vk::XcbSurfaceCreateInfoKHR({}, xcbConnection, mXCBWindow));
	#endif
}
stm::Window::~Window() {
	destroy_swapchain();
	mInstance->destroySurfaceKHR(mSurface);
#ifdef WIN32
	if (mHwnd) DestroyWindow(mHwnd);
#endif
#ifdef __linux
	if (mInstance.xcb_connection() && mXCBWindow)
 		xcb_destroy_window(mInstance.xcb_connection(), mXCBWindow);
#endif
}

Texture::View stm::Window::acquire_image(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("Window::acquire_image", commandBuffer);
	
	if (!mSwapchain) {
		create_swapchain(commandBuffer.mDevice);
		if (!mSwapchain) Texture::View();
	}

	mImageAvailableSemaphoreIndex = (mImageAvailableSemaphoreIndex+1)%mImageAvailableSemaphores.size();

	auto result = (*mSwapchainDevice)->acquireNextImageKHR(mSwapchain, numeric_limits<uint64_t>::max(), **mImageAvailableSemaphores[mImageAvailableSemaphoreIndex], {});
	if (result.result == vk::Result::eErrorOutOfDateKHR || result.result == vk::Result::eSuboptimalKHR) {
		destroy_swapchain();
		return Texture::View(); // swapchain failed to create (happens when window is minimized, etc)
	}
	mBackBufferIndex = result.value;
	return back_buffer();
}
void stm::Window::present(const vk::ArrayProxyNoTemporaries<const vk::Semaphore>& waitSemaphores) {
	ProfilerRegion ps("Window::present");
	auto result = mPresentQueueFamily->mQueues[0].presentKHR(vk::PresentInfoKHR(waitSemaphores, mSwapchain, mBackBufferIndex));
	mPresentCount++;
}

void stm::Window::resize(uint32_t w, uint32_t h) {
#ifdef WIN32
	RECT r;
	GetWindowRect(mHwnd, &r);
	int x = r.left, y = r.top;
	GetClientRect(mHwnd, &r);
	r.right = r.left + w;
	r.bottom = r.top + h;
	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	SetWindowPos(mHwnd, HWND_TOPMOST, x, y, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
#endif
}

void stm::Window::fullscreen(bool fs) {
	#ifdef WIN32
	if (fs && !mFullscreen) {
		GetWindowRect(mHwnd, &mWindowedRect);

		UINT WindowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
		SetWindowLongW(mHwnd, GWL_STYLE, WindowStyle);

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
	#endif

	#ifdef __linux
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

	//xcb_intern_atom_cookie_t cookie = xcb_intern_atom(mInstance.XCBConnection(), 0, 16, "_MOTIF_WM_HINTS");
	//xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(mInstance.XCBConnection(), cookie, NULL);
	//xcb_change_property(mInstance.XCBConnection(), XCB_PROP_MODE_REPLACE, mXCBWindow, reply->atom, reply->atom, 32, sizeof(hints), &hints);

	#endif
}

void stm::Window::create_swapchain(Device& device) {
	if (mSwapchain) destroy_swapchain();
	mSwapchainDevice = &device;
	mSwapchainDevice->set_debug_name(mSurface, "WindowSurface");
	
	mPresentQueueFamily = mSwapchainDevice->find_queue_family(mSurface);
	if (!mPresentQueueFamily) throw runtime_error("Device does not support the window surface!");

	vk::SurfaceCapabilitiesKHR capabilities = mSwapchainDevice->physical().getSurfaceCapabilitiesKHR(mSurface);
	auto formats 							 = mSwapchainDevice->physical().getSurfaceFormatsKHR(mSurface);
	auto presentModes 				 = mSwapchainDevice->physical().getSurfacePresentModesKHR(mSurface);
	auto queueFamilyProperties = mSwapchainDevice->physical().getQueueFamilyProperties();

	// get the size of the swapchain
	mSwapchainExtent = capabilities.currentExtent;
	if (mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mSwapchainExtent.width > mSwapchainDevice->limits().maxImageDimension2D || mSwapchainExtent.height > mSwapchainDevice->limits().maxImageDimension2D)
		return; // invalid swapchain size, window invalid

	// select the format of the swapchain
	mSurfaceFormat = formats[0];
	for (const vk::SurfaceFormatKHR& format : formats)
		if (format.format == vk::Format::eR8G8B8A8Unorm)
			mSurfaceFormat = format;
		else if (format.format == vk::Format::eB8G8R8A8Unorm)
			mSurfaceFormat = format;

	vector<vk::PresentModeKHR> preferredPresentModes { vk::PresentModeKHR::eMailbox };
	if (mAllowTearing) preferredPresentModes = { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate, vk::PresentModeKHR::eFifoRelaxed };
	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
	for (auto mode : preferredPresentModes)
		if (ranges::find(presentModes, mode) != presentModes.end())
			presentMode = mode;

	vk::SwapchainCreateInfoKHR createInfo = {};
	createInfo.surface = mSurface;
	createInfo.minImageCount = capabilities.minImageCount + 1;
	createInfo.imageFormat = mSurfaceFormat.format;
	createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
	createInfo.imageExtent = mSwapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	createInfo.imageSharingMode = vk::SharingMode::eExclusive;
	createInfo.preTransform = capabilities.currentTransform;
	createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	mSwapchain = (*mSwapchainDevice)->createSwapchainKHR(createInfo);
	mSwapchainDevice->set_debug_name(mSwapchain, "Window/Swapchain");
	
	// get the back buffers
	vector<vk::Image> images = (*mSwapchainDevice)->getSwapchainImagesKHR(mSwapchain);
	mSwapchainImages.resize(images.size());
	mImageAvailableSemaphores.clear();
	mImageAvailableSemaphores.resize(images.size());
	mBackBufferIndex = 0;
	mImageAvailableSemaphoreIndex = 0;
	
	// create per-frame image views and semaphores
	for (uint32_t i = 0; i < images.size(); i++) {
		mSwapchainDevice->set_debug_name(images[i], "swapchain"+to_string(i));
		mSwapchainImages[i] = Texture::View(
			make_shared<Texture>(images[i], *mSwapchainDevice, "swapchain"+to_string(i), vk::Extent3D(mSwapchainExtent,1), createInfo.imageFormat, createInfo.imageArrayLayers, 1, vk::SampleCountFlagBits::e1, createInfo.imageUsage),
			0, 1, 0, 1, vk::ImageAspectFlagBits::eColor);
		mImageAvailableSemaphores[i] = make_shared<Semaphore>(*mSwapchainDevice, "Swapchain/ImageAvaiableSemaphore" + to_string(i));
	}
}
void stm::Window::destroy_swapchain() {
	mSwapchainDevice->flush();
	mSwapchainImages.clear();
	mImageAvailableSemaphores.clear();
	if (mSwapchain) (*mSwapchainDevice)->destroySwapchainKHR(mSwapchain);
	mSwapchain = nullptr;
}

void stm::Window::lock_mouse(bool l) {
	#ifdef WIN32
	if (mLockMouse && !l)
		ShowCursor(TRUE);
	else if (!mLockMouse && l)
		ShowCursor(FALSE);
	#endif
	mLockMouse = l;
}

#ifdef WIN32
void Window::handle_message(UINT message, WPARAM wParam, LPARAM lParam) {
	vector<byte> lpb;
	switch (message) {
	case WM_DESTROY:
	case WM_QUIT:
		mHwnd = NULL;
		break;
	case WM_SIZE:
	case WM_MOVE: {
		RECT cr;
		GetClientRect(mHwnd, &cr);
		mClientRect.offset = vk::Offset2D( (int32_t)cr.top, (int32_t)cr.left );
		mClientRect.extent = vk::Extent2D( (uint32_t)((int32_t)cr.right - (int32_t)cr.left), (uint32_t)((int32_t)cr.bottom - (int32_t)cr.top) );
		break;
	}
	case WM_CHAR:
		mInputState.add_input_character(wParam);
	case WM_INPUT: {
		uint32_t dwSize = 0;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		lpb.resize(dwSize);
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) break;
		const RAWINPUT& raw = *reinterpret_cast<const RAWINPUT*>(lpb.data());
		if (raw.header.dwType == RIM_TYPEMOUSE) {
			mInputState.add_cursor_delta(Vector2f((float)raw.data.mouse.lLastX, (float)raw.data.mouse.lLastY));
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) 		mInputState.set_button  (KeyCode::eMouse1);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)  		mInputState.unset_button(KeyCode::eMouse1);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN)  	mInputState.set_button  (KeyCode::eMouse2);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)  		mInputState.unset_button(KeyCode::eMouse2);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) 		mInputState.set_button  (KeyCode::eMouse3);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP) 			mInputState.unset_button(KeyCode::eMouse3);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) 		mInputState.set_button  (KeyCode::eMouse4);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) 			mInputState.unset_button(KeyCode::eMouse4);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) 		mInputState.set_button  (KeyCode::eMouse5);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) 			mInputState.unset_button(KeyCode::eMouse5);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) 						mInputState.add_scroll_delta((float)bit_cast<SHORT>(raw.data.mouse.usButtonData) / (float)WHEEL_DELTA);
			if (mLockMouse) {
				RECT rect;
				GetWindowRect(mHwnd, &rect);
				SetCursorPos((rect.right + rect.left) / 2, (rect.bottom + rect.top) / 2);
			}
		}
		if (raw.header.dwType == RIM_TYPEKEYBOARD) {
			USHORT key = raw.data.keyboard.VKey;
			if (key == VK_LMENU || key == VK_RMENU) key = VK_MENU;
			else if (key == VK_LCONTROL || key == VK_RCONTROL) key = VK_CONTROL;
			else if (key == VK_LSHIFT || key == VK_RSHIFT) key = VK_SHIFT;
			
			if (raw.data.keyboard.Flags & RI_KEY_BREAK)
				mInputState.unset_button((KeyCode)key);
			else {
				mInputState.set_button((KeyCode)key);
				if (key == KeyCode::eKeyEnter && mInputState.pressed(KeyCode::eKeyAlt))
					fullscreen(!fullscreen());
			}
		}
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(mHwnd, &pt);
		mInputState.cursor_pos() = Vector2f((float)pt.x, (float)pt.y);
		break;
	}
	}
}
#endif

#ifdef __linux
bool stm::Window::process_event(xcb_generic_event_t* event) {
	xcb_motion_notify_event_t* mn = (xcb_motion_notify_event_t*)event;
	xcb_resize_request_event_t* rr = (xcb_resize_request_event_t*)event;
	xcb_button_press_event_t* bp = (xcb_button_press_event_t*)event;
	xcb_key_press_event_t* kp = (xcb_key_press_event_t*)event;
	xcb_key_release_event_t* kr = (xcb_key_release_event_t*)event;
	xcb_client_message_event_t* cm = (xcb_client_message_event_t*)event;

	KeyCode kc;

	switch (event->response_type & ~0x80) {
	case XCB_MOTION_NOTIFY:
		if (mn->same_screen)
			mInputState.cursor_pos() = Vector2f((float)mn->event_x, (float)mn->event_y);
		break;

	case XCB_KEY_PRESS:
		kc = (KeyCode)xcb_key_press_lookup_keysym(mInstance.xcb_key_symbols(), kp, 0);
		mInputState.set_button(kc);
		if (kc == KeyCode::eKeyEnter && (mInputState.pressed(KeyCode::eKeyLAlt) || mInputState.pressed(KeyCode::eKeyRAlt)))
			fullscreen(!fullscreen());
		break;
	case XCB_KEY_RELEASE:
		kc = (KeyCode)xcb_key_release_lookup_keysym(mInstance.xcb_key_symbols(), kp, 0);
		mInputState.unset_button(kc);
		break;

	case XCB_BUTTON_PRESS:
		if (bp->detail == 4) {
			mInputState.add_scroll_delta(1.f);
			break;
		}
		if (bp->detail == 5) {
			mInputState.add_scroll_delta(-1.f);
			break;
		}
	case XCB_BUTTON_RELEASE:
		switch (bp->detail){
		case 1:
			if ((event->response_type & ~0x80) == XCB_BUTTON_PRESS)
				mInputState.set_button(KeyCode::eMouse1);
			else
				mInputState.unset_button(KeyCode::eMouse1);
			break;
		case 2:
			if ((event->response_type & ~0x80) == XCB_BUTTON_PRESS)
				mInputState.set_button(KeyCode::eMouse3);
			else
				mInputState.unset_button(KeyCode::eMouse3);
			break;
		case 3:
			if ((event->response_type & ~0x80) == XCB_BUTTON_PRESS)
				mInputState.set_button(KeyCode::eMouse2);
			else
				mInputState.unset_button(KeyCode::eMouse2);
			break;
		}
		break;

	case XCB_CLIENT_MESSAGE:
		if (cm->data.data32[0] == mXCBDeleteWin)
			return false;
		break;
	}
	return true;
}
xcb_generic_event_t* stm::Window::poll_event() {
	xcb_generic_event_t* cur = xcb_poll_for_event(mInstance.xcb_connection());
	xcb_generic_event_t* nxt = xcb_poll_for_event(mInstance.xcb_connection());
	if (cur && (cur->response_type & ~0x80) == XCB_KEY_RELEASE && nxt && (nxt->response_type & ~0x80) == XCB_KEY_PRESS) {
		xcb_key_press_event_t* kp = (xcb_key_press_event_t*)cur;
		xcb_key_press_event_t* nkp = (xcb_key_press_event_t*)nxt;
		if (nkp->time == kp->time && nkp->detail == kp->detail) {
			free(cur);
			free(nxt);
			return poll_event(); // ignore repeat key press events
		}
	}
	if (cur) {
		if (!process_event(cur)) {
 			xcb_destroy_window(mInstance.xcb_connection(), mXCBWindow);
		 mXCBWindow = 0;
		}
		free(cur);
	}
	return nxt;
}
#endif