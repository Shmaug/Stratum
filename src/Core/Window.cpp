#include "Window.hpp"

#ifdef _WIN32
#pragma comment(lib, "Shell32.lib")
#include <shellapi.h>
#endif

using namespace stm;

stm::Window::Window(Instance& instance, const string& title, vk::Rect2D position) : mInstance(instance), mTitle(title), mClientRect(position) {
	#ifdef _WIN32

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
	DragAcceptFiles(mHwnd, TRUE);

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
	mInstance.device().flush();
	mSwapchainImages.clear();
	mRenderTargets.clear();
	mImageAvailableSemaphores.clear();
	if (mSwapchain) mInstance.device()->destroySwapchainKHR(mSwapchain);
	mSwapchain = nullptr;
	mInstance->destroySurfaceKHR(mSurface);
#ifdef _WIN32
	if (mHwnd) DestroyWindow(mHwnd);
#endif
#ifdef __linux
	if (mInstance.xcb_connection() && mXCBWindow)
 		xcb_destroy_window(mInstance.xcb_connection(), mXCBWindow);
#endif
}

bool stm::Window::acquire_image() {
	ProfilerRegion ps("Window::acquire_image");

	if (mRecreateSwapchain || !mSwapchain || mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0)
		create_swapchain();
	if (!mSwapchain || mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0) return false; // minimized ?

	const uint32_t semaphore_index = (mImageAvailableSemaphoreIndex + 1) % mImageAvailableSemaphores.size();

	vk::Result result = mInstance.device()->acquireNextImageKHR(mSwapchain, mAcquireImageTimeout.count(), **mImageAvailableSemaphores[semaphore_index], {}, &mBackBufferIndex);
	if (result == vk::Result::eNotReady || result == vk::Result::eTimeout)
		return false;

	if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR) {
		create_swapchain();
		if (!mSwapchain || mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0) return false; // minimized ?

		result = mInstance.device()->acquireNextImageKHR(mSwapchain, mAcquireImageTimeout.count(), **mImageAvailableSemaphores[semaphore_index], {}, &mBackBufferIndex);
		if (result == vk::Result::eNotReady || result == vk::Result::eTimeout || result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR)
			return false;
	}

	if (result != vk::Result::eSuccess)
		throw runtime_error("Failed to acquire next image");

	mImageAvailableSemaphoreIndex = semaphore_index;

	return true;
}
void stm::Window::resolve(CommandBuffer& commandBuffer) {
	commandBuffer.blit_image(back_buffer(), mSwapchainImages[back_buffer_index()]);
	mSwapchainImages[back_buffer_index()].transition_barrier(commandBuffer, vk::ImageLayout::ePresentSrcKHR);
}
void stm::Window::present(const vk::ArrayProxyNoTemporaries<const vk::Semaphore>& waitSemaphores) {
	ProfilerRegion ps("Window::present");
	auto result = mPresentQueueFamily->mQueues[0].presentKHR(vk::PresentInfoKHR(waitSemaphores, mSwapchain, mBackBufferIndex));
	mPresentCount++;
}

void stm::Window::resize(uint32_t w, uint32_t h) {
#ifdef _WIN32
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
	#ifdef _WIN32
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

void stm::Window::create_swapchain() {
	mPresentQueueFamily = mInstance.device().find_queue_family(mSurface);
	if (!mPresentQueueFamily) throw runtime_error("Device cannot present to the window surface!");

	// get the size of the swapchain
	vk::SurfaceCapabilitiesKHR capabilities = mPresentQueueFamily->mDevice.physical().getSurfaceCapabilitiesKHR(mSurface);
	mSwapchainExtent = capabilities.currentExtent;
	if (mSwapchainExtent.width == 0 || mSwapchainExtent.height == 0 || mSwapchainExtent.width > mPresentQueueFamily->mDevice.limits().maxImageDimension2D || mSwapchainExtent.height > mPresentQueueFamily->mDevice.limits().maxImageDimension2D)
		return; // invalid swapchain size, window invalid

	// select the format of the swapchain
	auto formats = mPresentQueueFamily->mDevice.physical().getSurfaceFormatsKHR(mSurface);
	mSurfaceFormat = formats.front();
	for (const vk::SurfaceFormatKHR& format : formats)
		if (format == mPreferredSurfaceFormat) {
			mSurfaceFormat = format;
			break;
		}

	mPresentMode = vk::PresentModeKHR::eFifo; // required to be supported
	for (const vk::PresentModeKHR& mode : mPresentQueueFamily->mDevice.physical().getSurfacePresentModesKHR(mSurface))
		if (mode == mPreferredPresentMode) {
			mPresentMode = mode;
			break;
		}

	if (mSwapchain) mInstance.device().flush();

	vk::SwapchainCreateInfoKHR createInfo = {};
	createInfo.surface = mSurface;
	createInfo.oldSwapchain = mSwapchain;
	createInfo.minImageCount = mMinImageCount;
	createInfo.imageFormat = mSurfaceFormat.format;
	createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
	createInfo.imageExtent = mSwapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	createInfo.imageSharingMode = vk::SharingMode::eExclusive;
	createInfo.preTransform = capabilities.currentTransform;
	createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	createInfo.presentMode = mPresentMode;
	createInfo.clipped = VK_FALSE;
	mSwapchain = mPresentQueueFamily->mDevice->createSwapchainKHR(createInfo);
	mPresentQueueFamily->mDevice.set_debug_name(mSwapchain, "Window/Swapchain");

	if (createInfo.oldSwapchain) mInstance.device()->destroySwapchainKHR(createInfo.oldSwapchain);

	// create per-frame image views and semaphores
	vector<vk::Image> images = mPresentQueueFamily->mDevice->getSwapchainImagesKHR(mSwapchain);
	mSwapchainImages.clear();
	mRenderTargets.clear();
	mImageAvailableSemaphores.clear();
	mSwapchainImages.reserve(images.size());
	mRenderTargets.reserve(images.size());
	mImageAvailableSemaphores.reserve(images.size());
	for (uint32_t i = 0; i < images.size(); i++) {
		mPresentQueueFamily->mDevice.set_debug_name(images[i], "swapchain " + to_string(i));
		mSwapchainImages.emplace_back(
			make_shared<Image>(images[i], mPresentQueueFamily->mDevice, "swapchain " + to_string(i), vk::Extent3D(mSwapchainExtent,1), createInfo.imageFormat, createInfo.imageArrayLayers, 1, vk::SampleCountFlagBits::e1, createInfo.imageUsage),
			0, 1, 0, 1, vk::ImageAspectFlagBits::eColor);
		mRenderTargets.emplace_back(
			make_shared<Image>(mPresentQueueFamily->mDevice, "render target " + to_string(i), vk::Extent3D(mSwapchainExtent,1), vk::Format::eR8G8B8A8Unorm, createInfo.imageArrayLayers, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled),
			0, 1, 0, 1, vk::ImageAspectFlagBits::eColor);
		mImageAvailableSemaphores.emplace_back(make_shared<Semaphore>(mPresentQueueFamily->mDevice, "Swapchain/ImageAvaiableSemaphore" + to_string(i)));
	}

	mBackBufferIndex = 0;
	mImageAvailableSemaphoreIndex = 0;
	mRecreateSwapchain = false;
}

#ifdef _WIN32
void Window::handle_message(UINT message, WPARAM wParam, LPARAM lParam) {
	vector<byte> lpb;
	switch (message) {
	case WM_DESTROY:
	case WM_QUIT:
		mHwnd = NULL;
		break;
	case WM_PAINT:
		mRepaint = true;
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
		break;
	case WM_INPUT: {
		uint32_t dwSize = 0;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		lpb.resize(dwSize);
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) break;
		const RAWINPUT& raw = *reinterpret_cast<const RAWINPUT*>(lpb.data());
		if (raw.header.dwType == RIM_TYPEMOUSE) {
			mInputState.add_cursor_delta(float2((float)raw.data.mouse.lLastX, (float)raw.data.mouse.lLastY));
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) 	mInputState.set_button  (KeyCode::eMouse1);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP)  	mInputState.unset_button(KeyCode::eMouse1);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN)  mInputState.set_button  (KeyCode::eMouse2);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP)  	mInputState.unset_button(KeyCode::eMouse2);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_DOWN) 	mInputState.set_button  (KeyCode::eMouse3);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_3_UP) 	mInputState.unset_button(KeyCode::eMouse3);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) 	mInputState.set_button  (KeyCode::eMouse4);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) 	mInputState.unset_button(KeyCode::eMouse4);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) 	mInputState.set_button  (KeyCode::eMouse5);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) 	mInputState.unset_button(KeyCode::eMouse5);
			if (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) 			mInputState.add_scroll_delta((float)bit_cast<SHORT>(raw.data.mouse.usButtonData) / (float)WHEEL_DELTA);
		} else if (raw.header.dwType == RIM_TYPEKEYBOARD) {
			USHORT key = raw.data.keyboard.VKey;
			if      (key == VK_LMENU 	|| key == VK_RMENU) 	key = VK_MENU;
			else if (key == VK_LCONTROL || key == VK_RCONTROL) 	key = VK_CONTROL;
			else if (key == VK_LSHIFT 	|| key == VK_RSHIFT) 	key = VK_SHIFT;

			if (raw.data.keyboard.Flags & RI_KEY_BREAK)
				mInputState.unset_button((KeyCode)key);
			else {
				mInputState.set_button((KeyCode)key);
				if (mInputState.pressed(KeyCode::eKeyAlt)) {
					if ((KeyCode)key == KeyCode::eKeyEnter)
						fullscreen(!fullscreen());
					else if ((KeyCode)key == KeyCode::eKeyF4) {
						DestroyWindow(mHwnd);
						mHwnd = nullptr;
					}
				}
			}
		}
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(mHwnd, &pt);
		mInputState.cursor_pos() = float2((float)pt.x, (float)pt.y);
		break;
	}
	case WM_DROPFILES: {
		HDROP hdrop = (HDROP)wParam;
		const UINT file_count = DragQueryFileA(hdrop, 0xFFFFFFFF, NULL, 0);
		char pathstr[MAX_PATH];
		for (uint32_t i = 0; i < file_count; i++) {
			if (DragQueryFileA(hdrop, i, pathstr, MAX_PATH))
				mInputState.mInputFiles.emplace_back(pathstr);
		}
		DragFinish(hdrop);
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
	xcb_keysym_t xkc;

	switch (event->response_type & ~0x80) {
	case XCB_MOTION_NOTIFY:
		if (mn->same_screen)
			mInputState.cursor_pos() = float2((float)mn->event_x, (float)mn->event_y);
		break;

	case XCB_KEY_PRESS:
		xkc = xcb_key_press_lookup_keysym(mInstance.xcb_key_symbols(), kp, 0);
		if (kc == XK_Shift_R) 	xkc = XK_Shift_L;
		if (kc == XK_Control_R) xkc = XK_Control_L;
		if (kc == XK_Alt_R) 		xkc = XK_Alt_L;
		kc = (KeyCode)xkc;
		mInputState.set_button(kc);
		if (kc == KeyCode::eKeyEnter && mInputState.pressed(KeyCode::eKeyAlt))
			fullscreen(!fullscreen());
		break;
	case XCB_KEY_RELEASE:
		xkc = xcb_key_release_lookup_keysym(mInstance.xcb_key_symbols(), kp, 0);
		if (kc == XK_Shift_R) 	xkc = XK_Shift_L;
		if (kc == XK_Control_R) xkc = XK_Control_L;
		if (kc == XK_Alt_R) 		xkc = XK_Alt_L;
		kc = (KeyCode)xkc;
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