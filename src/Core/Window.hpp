#pragma once

#include <Core/Device.hpp>
#include <Input/MouseKeyboardInput.hpp>

#ifdef WINDOWS
#include <vulkan/vulkan_win32.h>
#endif
#ifdef __linux
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
namespace x11{
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#endif

namespace stm {

class Window {
public:
	Instance* const mInstance;

	STRATUM_API ~Window();

	STRATUM_API void Fullscreen(bool fs);
	STRATUM_API void Resize(uint32_t w, uint32_t h);

	inline bool Fullscreen() const { return mFullscreen; }
	inline vk::Rect2D ClientRect() const { return mClientRect; };
	inline std::string Title() const { return mTitle; }

	inline uint32_t BackBufferCount() const { return (uint32_t)mSwapchainImages.size(); }
	inline uint32_t BackBufferIndex() const { return mBackBufferIndex; }
	inline bool VSync() const { return mVSync; }
	inline void VSync(bool v) { mVSync = v; }
	
	inline vk::SurfaceKHR Surface() const { return mSurface; }
	inline vk::SwapchainKHR Swapchain() const { return mSwapchain; }
	inline vk::SurfaceFormatKHR SurfaceFormat() const { return mSurfaceFormat; }
	inline vk::Extent2D SwapchainExtent() const { return mSwapchainExtent; }
	inline vk::Image BackBuffer() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].first; }
	inline vk::Image BackBuffer(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].first; }
	inline vk::ImageView BackBufferView() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].second; }
	inline vk::ImageView BackBufferView(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].second; }
	inline Semaphore* ImageAvailableSemaphore() const { return mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }
	inline uint32_t PresentQueueFamily() const { return mPresentQueueFamily; }

	#ifdef WINDOWS
	inline HWND Hwnd() const { return mHwnd; }
	#endif
	
private:
	friend class Instance;

	STRATUM_API Window(Instance* instance, const std::string& title, MouseKeyboardInput* input, vk::Rect2D position
	#ifdef __linux
		, xcb_connection_t* XCBConnection, xcb_screen_t* XCBScreen);
	#else
		, HINSTANCE hInst);
	#endif

	STRATUM_API void AcquireNextImage();
	/// Waits on all semaphores in waitSemaphores
	STRATUM_API void Present(const std::set<vk::Semaphore>& waitSemaphores);
	STRATUM_API void CreateSwapchain(stm::Device* device);
	STRATUM_API void DestroySwapchain();

	vk::SurfaceKHR mSurface;
	Device* mSwapchainDevice = nullptr;
	vk::SwapchainKHR mSwapchain;
	uint32_t mPresentQueueFamily;
	std::vector<std::pair<vk::Image, vk::ImageView>> mSwapchainImages;
	std::vector<Semaphore*> mImageAvailableSemaphores;
	vk::Extent2D mSwapchainExtent;
	vk::SurfaceFormatKHR mSurfaceFormat;
	uint32_t mBackBufferIndex = 0;
	uint32_t mImageAvailableSemaphoreIndex = 0;

	vk::DisplayKHR mDirectDisplay;

	bool mFullscreen = false;
	bool mVSync = false;
	vk::Rect2D mClientRect;
	std::string mTitle;

	#ifdef WINDOWS
	HWND mHwnd = 0;
	RECT mWindowedRect;
	#endif
	#ifdef __linux
	xcb_connection_t* mXCBConnection = nullptr;
	xcb_screen_t* mXCBScreen = nullptr;
	xcb_window_t mXCBWindow;
	xcb_atom_t mXCBProtocols;
	xcb_atom_t mXCBDeleteWin;
	vk::Rect2D mWindowedRect;
	#endif

	MouseKeyboardInput* mInput = nullptr;
};

}