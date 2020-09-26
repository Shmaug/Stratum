#pragma once

#include <openxr/openxr.h>

#include "PluginManager.hpp"
#include <Input/InputManager.hpp>
#include <Input/MouseKeyboardInput.hpp>

#ifdef __linux
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <vulkan/vulkan_xcb.h>
namespace x11{
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#endif

namespace stm {

class Instance {
public:
	Instance() = delete;
	Instance(const Instance&) = delete;
	Instance(Instance&&) = delete;
	Instance& operator=(const Instance&) = delete;
	Instance& operator=(Instance&&) = delete;
	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();
	inline vk::Instance operator*() const { return mInstance; }
	inline const vk::Instance* operator->() const { return &mInstance; }

	// advance swapchain & poll events. return false if the program should exit
	STRATUM_API bool AdvanceSwapchain();
	STRATUM_API void Present(const std::set<vk::Semaphore>& waitSemaphores);

	inline bool GetOptionExists(const std::string& name) const {
		return mOptions.count(name);
	}
	inline bool GetOption(const std::string& name, std::string& value) const {
		if (mOptions.count(name)) {
			value = mOptions.at(name);
			return true;
		}
		return false;
	}
	
	inline stm::PluginManager& PluginManager() { return mPluginManager; }
	inline stm::InputManager& InputManager() { return mInputManager; }
	inline stm::Device* Device() const { return mDevice; }
	inline stm::Window* Window() const { return mWindow; }
	
	static bool sDisableDebugCallback;

private:
	vk::Instance mInstance;
	stm::Device* mDevice = nullptr;
	stm::Window* mWindow = nullptr;
	stm::PluginManager mPluginManager;
	stm::InputManager mInputManager;
	MouseKeyboardInput* mMouseKeyboardInput = nullptr;
	XrInstance xrInstance = XR_NULL_HANDLE;

	std::vector<std::string> mCommandLine;
	std::map<std::string, std::string> mOptions;

	bool mDestroyPending = false;
	STRATUM_API bool PollEvents();
	
	vk::DebugUtilsMessengerEXT mDebugMessenger;

	#ifdef __linux
	STRATUM_API void ProcessEvent(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* PollEvent();

	x11::Display* mXDisplay;
	xcb_connection_t* mXCBConnection;
	xcb_key_symbols_t* mXCBKeySymbols;
	#endif
	#ifdef WINDOWS
	void HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	#endif

};

}