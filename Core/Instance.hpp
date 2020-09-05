#pragma once

#include <Util/Util.hpp>

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

#include <Input/MouseKeyboardInput.hpp>
#include <openxr/openxr.h>

class Instance {
private:
	vk::Instance mInstance;

public:
	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();

	// Poll events, advance swapchain. Returns false if the program should exit
	STRATUM_API bool BeginFrame();
	// Present swapchain
	STRATUM_API void EndFrame(const std::set<vk::Semaphore>& waitSemaphores);

	inline const std::vector<std::string>::const_iterator ArgsBegin() const { return mCmdArguments.begin(); }
	inline const std::vector<std::string>::const_iterator ArgsEnd() const { return mCmdArguments.end(); }
	
	inline ::Device* Device() const { return mDevice; }
	inline ::Window* Window() const { return mWindow; }

	inline ::PluginManager* PluginManager() const { return mPluginManager; }
	inline ::InputManager* InputManager() const { return mInputManager; }
	inline operator vk::Instance() const { return mInstance; }

private:
	::Device* mDevice = nullptr;
	::Window* mWindow = nullptr;
	::PluginManager* mPluginManager = nullptr;
	::InputManager* mInputManager = nullptr;
	MouseKeyboardInput* mMouseKeyboardInput = nullptr;

	XrInstance xrInstance = XR_NULL_HANDLE;

	#ifdef ENABLE_DEBUG_LAYERS
	vk::DebugUtilsMessengerEXT mDebugMessenger;
	#endif

	std::vector<std::string> mCmdArguments;

	bool mDestroyPending = false;
	#ifdef __linux
	STRATUM_API void ProcessEvent(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* PollEvent();

	x11::Display* mXDisplay;
	xcb_connection_t* mXCBConnection;
	xcb_key_symbols_t* mXCBKeySymbols;
	#else
	void HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	#endif

	STRATUM_API bool PollEvents();
	
public:
	static bool sDisableDebugCallback;
};