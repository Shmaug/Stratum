#pragma once

#include "../Stratum.hpp"

namespace stm {

class Device;
class Window;

class Instance {
public:
	static bool sDisableDebugCallback;

	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();

	inline const vk::Instance& operator*() const { return mInstance; }
	inline const vk::Instance* operator->() const { return &mInstance; }

	inline uint32_t vulkan_version() const { return mVulkanApiVersion; }

	inline stm::Device& device() const { return *mDevice; }
	inline stm::Window& window() const { return *mWindow; }
	
	inline string find_argument(const string& name) const { return mOptions.at(name); }
	inline bool find_argument(const string& name, string& value) const {
		if (mOptions.count(name)) {
			value = mOptions.at(name);
			return true;
		}
		return false;
	}
	
	STRATUM_API bool poll_events();

	#ifdef WIN32
	inline HINSTANCE hInstance() const { return mHInstance; }
	#elif defined(__linux)
	inline xcb_window_t XCBScreen() const { return mXCBScreen; }
	#endif

private:
	vk::Instance mInstance;
	unique_ptr<stm::Device> mDevice;
	unique_ptr<stm::Window> mWindow;
	vector<string> mCommandLine;
	unordered_map<string, string> mOptions;
	uint32_t mVulkanApiVersion;

	vk::DebugUtilsMessengerEXT mDebugMessenger;

	bool mDestroyPending = false;
	
	#ifdef WIN32
	void handle_message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	HINSTANCE mHInstance;
	#elif defined(__linux)
	STRATUM_API void ProcessEvent(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* PollEvent();
	x11::Display* mXDisplay;
	xcb_connection_t* mXCBConnection;
	xcb_key_symbols_t* mXCBKeySymbols;
	xcb_screen_t* mXCBScreen = nullptr;
	#endif
};

}