#pragma once

#include <Util/Util.hpp>

#ifdef __linux
#include <vulkan/vulkan.h>
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
#include <XR/XRRuntime.hpp>

class Instance {
public:
	static bool sDisableDebugCallback;

	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();

	// Poll events, advance swapchain. Returns false if the program should exit
	STRATUM_API bool BeginFrame();
	// Present swapchain
	STRATUM_API void EndFrame(const std::vector<VkSemaphore>& waitSemaphores);

	inline const std::vector<std::string>& CommandLineArguments() const { return mCmdArguments; }
	
	inline ::Device* Device() const { return mDevice; }
	inline ::Window* Window() const { return mWindow; }
	inline const std::vector<XRRuntime*>& XRRuntimes() const { return mXRRuntimes; }

	inline ::PluginManager* PluginManager() const { return mPluginManager; }
	inline ::InputManager* InputManager() const { return mInputManager; }
	inline operator VkInstance() const { return mInstance; }

private:
	VkInstance mInstance;
	::Device* mDevice;
	::Window* mWindow;
	::PluginManager* mPluginManager;
	::InputManager* mInputManager;
	MouseKeyboardInput* mMouseKeyboardInput;
	std::vector<XRRuntime*> mXRRuntimes;

	#ifdef ENABLE_DEBUG_LAYERS
	VkDebugUtilsMessengerEXT mDebugMessenger;
	#endif

	std::vector<std::string> mCmdArguments;

	bool mDestroyPending;
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
};