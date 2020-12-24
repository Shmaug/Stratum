#pragma once

#include "../Stratum.hpp"

namespace stm {

class Instance {
public:
	static bool sDisableDebugCallback;

	Instance() = delete;
	Instance(const Instance&) = delete;
	Instance(Instance&&) = delete;
	STRATUM_API Instance(int argc, char** argv);
	STRATUM_API ~Instance();
	Instance& operator=(const Instance&) = delete;
	Instance& operator=(Instance&&) = delete;
	inline vk::Instance operator*() const { return mInstance; }
	inline const vk::Instance* operator->() const { return &mInstance; }

	inline string TryGetOption(const string& name) const { return mOptions.at(name); }
	inline bool TryGetOption(const string& name, string& value) const {
		if (mOptions.count(name)) {
			value = mOptions.at(name);
			return true;
		}
		return false;
	}
	
	// advance swapchain & poll events. return false if the program should exit
	STRATUM_API bool AdvanceFrame();
	STRATUM_API void PresentFrame(const set<vk::Semaphore>& waitSemaphores = {});

	inline stm::Device& Device() const { return *mDevice; }
	inline stm::Window& Window() const { return *mWindow; }
	
	#ifdef WINDOWS
	inline HINSTANCE HInstance() const { return mHInstance; }
	#endif
	#ifdef __linux
	inline xcb_window_t XCBScreen() const { return mXCBScreen; }
	#endif

private:
	STRATUM_API bool PollEvents();

	vk::Instance mInstance;
	stm::Device* mDevice;
	stm::Window* mWindow;
	vector<string> mCommandLine;
	map<string, string> mOptions;

	vk::DebugUtilsMessengerEXT mDebugMessenger;

	bool mDestroyPending = false;
	
	#ifdef WINDOWS
	void HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	HINSTANCE mHInstance;
	#endif
	#ifdef __linux
	STRATUM_API void ProcessEvent(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* PollEvent();
	x11::Display* mXDisplay;
	xcb_connection_t* mXCBConnection;
	xcb_key_symbols_t* mXCBKeySymbols;
	xcb_screen_t* mXCBScreen = nullptr;
	#endif
};

}