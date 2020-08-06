#pragma once

#include <Util/Util.hpp>

#ifdef __linux
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
namespace x11{
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#else
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <Core/Device.hpp>
#include <Input/MouseKeyboardInput.hpp>

class Window {
public:
	STRATUM_API ~Window();

	STRATUM_API void Fullscreen(bool fs);
	STRATUM_API void Resize(uint32_t w, uint32_t h);

	inline VkSurfaceKHR Surface() const { return mSurface; }
	inline VkSurfaceFormatKHR Format() const { return mFormat; }

	inline bool Fullscreen() const { return mFullscreen; }
	inline VkRect2D ClientRect() const { return mClientRect; };
	inline std::string Title() const { return mTitle; }

	inline uint32_t BackBufferCount() const { return mImageCount; }
	inline uint32_t BackBufferIndex() const { return mBackBufferIndex; }
	inline bool VSync() const { return mVSync; }
	inline void VSync(bool v) { mVSync = v; }
	
	inline VkImage BackBuffer() const { if (!mFrameData) return VK_NULL_HANDLE; return mFrameData[mBackBufferIndex].mSwapchainImage; }
	inline VkImage BackBuffer(uint32_t i) const { if (!mFrameData) return VK_NULL_HANDLE; return mFrameData[i].mSwapchainImage; }
	inline VkImageView BackBufferView() const { if (!mFrameData) return VK_NULL_HANDLE; return mFrameData[mBackBufferIndex].mSwapchainImageView; }
	inline VkImageView BackBufferView(uint32_t i) const { if (!mFrameData) return VK_NULL_HANDLE; return mFrameData[i].mSwapchainImageView; }
	inline VkExtent2D SwapchainExtent() const { return mSwapchainExtent; }

	inline Semaphore* ImageAvailableSemaphore() const { return mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }

	#ifdef WINDOWS
	inline HWND Hwnd() const { return mHwnd; }
	#endif
	inline ::Device* Device() const { return mDevice; }

private:
	friend class Instance;

	#ifdef __linux
	STRATUM_API Window(Instance* instance, const std::string& title, MouseKeyboardInput* input, VkRect2D position, xcb_connection_t* XCBConnection, xcb_screen_t* XCBScreen);
	#else
	STRATUM_API Window(Instance* instance, const std::string& title, MouseKeyboardInput* input, VkRect2D position, HINSTANCE hInst);
	#endif

	STRATUM_API VkImage AcquireNextImage();
	/// Waits on all semaphores in waitSemaphores
	STRATUM_API void Present(const std::vector<VkSemaphore>& waitSemaphores);

	MouseKeyboardInput* mInput;

	bool mFullscreen;
	bool mVSync;
	VkRect2D mClientRect;
	std::string mTitle;

	VkDisplayKHR mDirectDisplay;

	Instance* mInstance;
	::Device* mDevice;
	VkPhysicalDevice mPhysicalDevice;

	#ifdef __linux
	xcb_connection_t* mXCBConnection;
	xcb_screen_t* mXCBScreen;
	xcb_window_t mXCBWindow;
	xcb_atom_t mXCBProtocols;
	xcb_atom_t mXCBDeleteWin;
	VkRect2D mWindowedRect;
	#else
	HWND mHwnd;
	RECT mWindowedRect;
	#endif

	VkSurfaceKHR mSurface;
	VkSwapchainKHR mSwapchain;

	VkExtent2D mSwapchainExtent;
	VkSurfaceFormatKHR mFormat;
	uint32_t mImageCount;
	uint32_t mBackBufferIndex;

	struct FrameData {
		VkImage mSwapchainImage;
		VkImageView mSwapchainImageView;
		inline FrameData() : mSwapchainImage(VK_NULL_HANDLE), mSwapchainImageView(VK_NULL_HANDLE) {};
	};
	FrameData* mFrameData;

	/// semaphores that signal when an image is available (via vkAcquireNextImageKHR)
	std::vector<Semaphore*> mImageAvailableSemaphores;
	uint32_t mImageAvailableSemaphoreIndex;

	STRATUM_API void CreateSwapchain(::Device* device);
	STRATUM_API void DestroySwapchain();
};