#pragma once

#include "../Common/common.hpp"

#ifdef WIN32
#include <vulkan/vulkan_win32.h>
#endif
#ifdef __linux
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <vulkan/vulkan_xcb.h>
namespace x11 {
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#endif

namespace stm {

class Device;
class Window;

class Instance {
public:
	static bool sDisableDebugCallback;

	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();

	inline vk::Instance& operator*() { return mInstance; }
	inline vk::Instance* operator->() { return &mInstance; }
	inline const vk::Instance& operator*() const { return mInstance; }
	inline const vk::Instance* operator->() const { return &mInstance; }

	inline uint32_t vulkan_version() const { return mVulkanApiVersion; }

	inline stm::Device& device() const { return *mDevice; }
	inline stm::Window& window() const { return *mWindow; }
	
	inline bool find_argument(const string& name) const {
		return mOptions.find(name) != mOptions.end();
	}
	inline auto find_arguments(const string& name) const {
		auto[first,last] = mOptions.equal_range(name);
		return ranges::subrange(first,last) | views::values;
	}

	#ifdef WIN32
	inline HINSTANCE hInstance() const { return mHInstance; }
	#endif
	#ifdef __linux
	inline xcb_connection_t* xcb_connection() const { return mXCBConnection; }
	inline xcb_key_symbols_t* xcb_key_symbols() const { return mXCBKeySymbols; }
	inline xcb_screen_t* xcb_screen() const { return mXCBScreen; }
	inline x11::Display* x_display() const { return mXDisplay; }
	#endif

	STRATUM_API void poll_events();

private:
	vk::Instance mInstance;
	unique_ptr<stm::Device> mDevice;
	unique_ptr<stm::Window> mWindow;
	vector<string> mCommandLine;
	unordered_multimap<string, string> mOptions;
	uint32_t mVulkanApiVersion;

	vk::DebugUtilsMessengerEXT mDebugMessenger;

	bool mDestroyPending = false;

	friend class stm::Window;
	
	#ifdef WIN32
	static LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	HINSTANCE mHInstance;
	#endif

	#ifdef __linux
	xcb_connection_t* mXCBConnection;
	xcb_screen_t* mXCBScreen = nullptr;
	x11::Display* mXDisplay;
	xcb_key_symbols_t* mXCBKeySymbols;
	#endif
};

}