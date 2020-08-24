#pragma once

#include <Util/Util.hpp>

#ifdef __linux
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
namespace x11{
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#else
#include <vulkan/vulkan_win32.h>
#endif

#include <Core/Device.hpp>
#include <Input/MouseKeyboardInput.hpp>

class Window {
public:
	STRATUM_API ~Window();

	STRATUM_API void Fullscreen(bool fs);
	STRATUM_API void Resize(uint32_t w, uint32_t h);

	inline vk::SurfaceKHR Surface() const { return mSurface; }
	inline vk::SurfaceFormatKHR Format() const { return mFormat; }

	inline bool Fullscreen() const { return mFullscreen; }
	inline vk::Rect2D ClientRect() const { return mClientRect; };
	inline std::string Title() const { return mTitle; }

	inline uint32_t BackBufferCount() const { return (uint32_t)mSwapchainImages.size(); }
	inline uint32_t BackBufferIndex() const { return mBackBufferIndex; }
	inline bool VSync() const { return mVSync; }
	inline void VSync(bool v) { mVSync = v; }
	
	inline vk::Extent2D SwapchainExtent() const { return mSwapchainExtent; }
	inline vk::Image BackBuffer() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].first; }
	inline vk::Image BackBuffer(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].first; }
	inline vk::ImageView BackBufferView() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].second; }
	inline vk::ImageView BackBufferView(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].second; }
	inline Semaphore* ImageAvailableSemaphore() const { return mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }

	#ifdef WINDOWS
	inline HWND Hwnd() const { return mHwnd; }
	#endif
	inline ::Device* Device() const { return mDevice; }

private:
	friend class Instance;

	STRATUM_API Window(Instance* instance, const std::string& title, MouseKeyboardInput* input, vk::Rect2D position
	#ifdef __linux
		, xcb_connection_t* XCBConnection, xcb_screen_t* XCBScreen);
	#else
		, HINSTANCE hInst);
	#endif

	STRATUM_API vk::Image AcquireNextImage();
	/// Waits on all semaphores in waitSemaphores
	STRATUM_API void Present(const std::vector<vk::Semaphore>& waitSemaphores);
	STRATUM_API void CreateSwapchain(::Device* device);
	STRATUM_API void DestroySwapchain();

	vk::SurfaceKHR mSurface;
	vk::SwapchainKHR mSwapchain;
	std::vector<std::pair<vk::Image, vk::ImageView>> mSwapchainImages;
	std::vector<Semaphore*> mImageAvailableSemaphores;

	Instance* mInstance = nullptr;
	::Device* mDevice = nullptr;
	vk::PhysicalDevice mPhysicalDevice;

	vk::DisplayKHR mDirectDisplay;

	vk::Extent2D mSwapchainExtent;
	vk::SurfaceFormatKHR mFormat;
	uint32_t mBackBufferIndex = 0;
	uint32_t mImageAvailableSemaphoreIndex = 0;

	bool mFullscreen = false;
	bool mVSync = false;
	vk::Rect2D mClientRect;
	std::string mTitle;

	#ifdef __linux
	xcb_connection_t* mXCBConnection = nullptr;
	xcb_screen_t* mXCBScreen = nullptr;
	xcb_window_t mXCBWindow;
	xcb_atom_t mXCBProtocols;
	xcb_atom_t mXCBDeleteWin;
	vk::Rect2D mWindowedRect;
	#else
	HWND mHwnd = 0;
	RECT mWindowedRect;
	#endif

	MouseKeyboardInput* mInput = nullptr;
};