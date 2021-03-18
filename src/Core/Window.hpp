#pragma once

#include "Device.hpp"
#include "InputState.hpp"

#ifdef WIN32
#include <vulkan/vulkan_win32.h>
#endif
#ifdef __linux
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
namespace x11 {
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <vulkan/vulkan_xlib_xrandr.h>
};
#endif

namespace stm {

enum KeyCode {
#ifdef WIN32
	KEY_NONE = 0x00,

	MOUSE_LEFT = 0x01,
	MOUSE_RIGHT = 0x02,
	KEY_CANCEL = 0x03,
	MOUSE_MIDDLE = 0x04,
	MOUSE_X1 = 0x05,
	MOUSE_X2 = 0x06,

	KEY_BACKSPACE = 0x08,
	KEY_TAB = 0x09,
	KEY_ENTER = 0x0d,
	KEY_LOCK_CAPS = 0x14,
	KEY_ESCAPE = 0x1b,

	KEY_SPACE = 0x20,
	KEY_PAGEUP = 0x21,
	KEY_PAGEDOWN = 0x22,
	KEY_END = 0x23,
	KEY_HOME = 0x24,
	KEY_LEFT = 0x25,
	KEY_UP = 0x26,
	KEY_RIGHT = 0x27,
	KEY_DOWN = 0x28,
	KEY_PRINTSCREEN = 0x2c,
	KEY_INSERT = 0X2d,
	KEY_DELETE = 0X2e,
	KEY_D0 = 0x30,
	KEY_D1 = 0x31,
	KEY_D2 = 0x32,
	KEY_D3 = 0x33,
	KEY_D4 = 0x34,
	KEY_D5 = 0x35,
	KEY_D6 = 0x36,
	KEY_D7 = 0x37,
	KEY_D8 = 0x38,
	KEY_D9 = 0x39,
	KEY_A = 0x41,
	KEY_B = 0x42,
	KEY_C = 0x43,
	KEY_D = 0x44,
	KEY_E = 0x45,
	KEY_F = 0x46,
	KEY_G = 0x47,
	KEY_H = 0x48,
	KEY_I = 0x49,
	KEY_J = 0x4a,
	KEY_K = 0x4b,
	KEY_L = 0x4c,
	KEY_M = 0x4d,
	KEY_N = 0x4e,
	KEY_O = 0x4f,
	KEY_P = 0x50,
	KEY_Q = 0x51,
	KEY_R = 0x52,
	KEY_S = 0x53,
	KEY_T = 0x54,
	KEY_U = 0x55,
	KEY_V = 0x56,
	KEY_W = 0x57,
	KEY_X = 0x58,
	KEY_Y = 0x59,
	KEY_Z = 0x5a,
	KEY_NUMPAD0 = 0x60,
	KEY_NUMPAD1 = 0x61,
	KEY_NUMPAD2 = 0x62,
	KEY_NUMPAD3 = 0x63,
	KEY_NUMPAD4 = 0x64,
	KEY_NUMPAD5 = 0x65,
	KEY_NUMPAD6 = 0x66,
	KEY_NUMPAD7 = 0x67,
	KEY_NUMPAD8 = 0x68,
	KEY_NUMPAD9 = 0x69,
	KEY_MULTIPLY = 0x6a,
	KEY_ADD = 0x6b,
	KEY_SEPARATOR = 0x6c,
	KEY_SUBTRACT = 0x6d,
	KEY_DECIMAL = 0x6e,
	KEY_DIVIDE = 0x6f,
	KEY_F1 = 0x70,
	KEY_F2 = 0x71,
	KEY_F3 = 0x72,
	KEY_F4 = 0x73,
	KEY_F5 = 0x74,
	KEY_F6 = 0x75,
	KEY_F7 = 0x76,
	KEY_F8 = 0x77,
	KEY_F9 = 0x78,
	KEY_F10 = 0x79,
	KEY_F11 = 0x7a,
	KEY_F12 = 0x7b,
	KEY_NUM_LOCK = 0x90,
	KEY_SCROLL_LOCK = 0x91,
	KEY_LSHIFT = 0xa0,
	KEY_RSHIFT = 0xa1,
	KEY_LCONTROL = 0xa2,
	KEY_RCONTROL = 0xa3,
	KEY_LALT = 0xa4,
	KEY_RALT = 0xa5,

	KEY_SEMICOLON = 0xba, // Used for miscellaneous characters; it can vary by keyboard.  For the US standard keyboard, the ';:' key
	KEY_EQUAL = 0xbb, // For any country/region, the '+' key
	KEY_COMMA = 0xbc, // For any country/region, the ',' key
	KEY_MINUS = 0xbd, // For any country/region, the '-' key
	KEY_PERIOD = 0xbe, // For any country/region, the '.' key
	KEY_SLASH = 0xbf, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the '/?' key
	KEY_TILDE = 0xc0, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the '`~' key
	KEY_BRACKET_L = 0xdb, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the '[{' key
	KEY_BACKSLASH = 0xdc, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the '\|' key
	KEY_BRACKET_R = 0xdd, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the ']}' key
	KEY_QUOTE = 0xde, // Used for miscellaneous characters; it can vary by keyboard. For the US standard keyboard, the 'single-quote/double-quote' key
#endif
#ifdef __linux
	KEY_NONE = 0x0000,

	MOUSE_LEFT = 0x001,
	MOUSE_RIGHT = 0x002,
	KEY_CANCEL = 0x003,
	MOUSE_MIDDLE = 0x004,
	MOUSE_X1 = 0x005,
	MOUSE_X2 = 0x006,

	KEY_BACKSPACE = x11::XK_BackSpace,
	KEY_TAB = x11::XK_Tab,
	KEY_ENTER = x11::XK_Return,
	KEY_LOCK_CAPS = x11::XK_Caps_Lock,
	KEY_ESCAPE = x11::XK_Escape,

	KEY_SPACE = x11::XK_space,
	KEY_PAGEUP = x11::XK_Page_Up,
	KEY_PAGEDOWN = x11::XK_Page_Down,
	KEY_END = x11::XK_End,
	KEY_HOME = x11::XK_Home,
	KEY_LEFT = x11::XK_Left,
	KEY_UP = x11::XK_Up,
	KEY_RIGHT = x11::XK_Right,
	KEY_DOWN = x11::XK_Down,
	KEY_INSERT = x11::XK_Insert,
	KEY_DELETE = x11::XK_Delete,
	KEY_D0 = x11::XK_0,
	KEY_D1 = x11::XK_1,
	KEY_D2 = x11::XK_2,
	KEY_D3 = x11::XK_3,
	KEY_D4 = x11::XK_4,
	KEY_D5 = x11::XK_5,
	KEY_D6 = x11::XK_6,
	KEY_D7 = x11::XK_7,
	KEY_D8 = x11::XK_8,
	KEY_D9 = x11::XK_9,
	KEY_A = x11::XK_a,
	KEY_B = x11::XK_b,
	KEY_C = x11::XK_c,
	KEY_D = x11::XK_d,
	KEY_E = x11::XK_e,
	KEY_F = x11::XK_f,
	KEY_G = x11::XK_g,
	KEY_H = x11::XK_h,
	KEY_I = x11::XK_i,
	KEY_J = x11::XK_j,
	KEY_K = x11::XK_k,
	KEY_L = x11::XK_l,
	KEY_M = x11::XK_m,
	KEY_N = x11::XK_n,
	KEY_O = x11::XK_o,
	KEY_P = x11::XK_p,
	KEY_Q = x11::XK_q,
	KEY_R = x11::XK_r,
	KEY_S = x11::XK_s,
	KEY_T = x11::XK_t,
	KEY_U = x11::XK_u,
	KEY_V = x11::XK_v,
	KEY_W = x11::XK_w,
	KEY_X = x11::XK_x,
	KEY_Y = x11::XK_y,
	KEY_Z = x11::XK_z,
	KEY_NUMPAD0 = x11::XK_KP_Insert,
	KEY_NUMPAD1 = x11::XK_KP_End,
	KEY_NUMPAD2 = x11::XK_KP_Down,
	KEY_NUMPAD3 = x11::XK_KP_Page_Down,
	KEY_NUMPAD4 = x11::XK_KP_Left,
	KEY_NUMPAD5 = x11::XK_KP_Begin,
	KEY_NUMPAD6 = x11::XK_KP_Right,
	KEY_NUMPAD7 = x11::XK_KP_Home,
	KEY_NUMPAD8 = x11::XK_KP_Up,
	KEY_NUMPAD9 = x11::XK_KP_Page_Up,
	KEY_MULTIPLY = x11::XK_KP_Multiply,
	KEY_ADD = x11::XK_KP_Add,
	KEY_NUMPAD_ENTER = x11::XK_KP_Enter,
	KEY_SUBTRACT = x11::XK_KP_Subtract,
	KEY_DECIMAL = x11::XK_KP_Delete,
	KEY_DIVIDE = x11::XK_KP_Divide,
	KEY_F1 = x11::XK_F1,
	KEY_F2 = x11::XK_F2,
	KEY_F3 = x11::XK_F3,
	KEY_F4 = x11::XK_F4,
	KEY_F5 = x11::XK_F5,
	KEY_F6 = x11::XK_F6,
	KEY_F7 = x11::XK_F7,
	KEY_F8 = x11::XK_F8,
	KEY_F9 = x11::XK_F9,
	KEY_F10 = x11::XK_F10,
	KEY_F11 = x11::XK_F11,
	KEY_F12 = x11::XK_F12,
	KEY_NUM_LOCK = x11::XK_Num_Lock,
	KEY_SCROLL_LOCK = x11::XK_Scroll_Lock,
	KEY_LSHIFT = x11::XK_Shift_L,
	KEY_RSHIFT = x11::XK_Shift_R,
	KEY_LCONTROL = x11::XK_Control_L,
	KEY_RCONTROL = x11::XK_Control_R,
	KEY_LALT = x11::XK_Alt_L,
	KEY_RALT = x11::XK_Alt_R,

	KEY_SEMICOLON = x11::XK_semicolon,
	KEY_EQUAL = x11::XK_equal,
	KEY_COMMA = x11::XK_comma,
	KEY_MINUS = x11::XK_minus,
	KEY_PERIOD = x11::XK_period,
	KEY_SLASH = x11::XK_slash,
	KEY_TILDE = x11::XK_grave,
	KEY_BRACKET_L = x11::XK_bracketleft,
	KEY_BRACKET_R = x11::XK_bracketright,
	KEY_QUOTE = x11::XK_quotedbl,
	KEY_BACKSLASH = x11::XK_backslash,
#endif
};

class MouseState : public InputState {
public:
	Vector2f mCursorPos = Vector2f(0);
	Vector2f mCursorDelta = Vector2f(0);
	float mScrollDelta = 0;
	unordered_map<KeyCode, bool> mKeys;
	inline MouseState() : InputState("Mouse") {};
};

class Window {
public:
	Instance& mInstance;
	
	STRATUM_API Window(Instance& instance, const string& title, vk::Rect2D position);
	STRATUM_API ~Window();

	STRATUM_API void Resize(uint32_t w, uint32_t h);
	STRATUM_API void Fullscreen(bool fs);
	inline void AllowTearing(bool v) { mAllowTearing = v; }

	inline vk::SurfaceKHR Surface() const { return mSurface; }
	inline vk::SwapchainKHR Swapchain() const { return mSwapchain; }
	inline vk::SurfaceFormatKHR SurfaceFormat() const { return mSurfaceFormat; }
	inline vk::Extent2D SwapchainExtent() const { return mSwapchainExtent; }
	inline vk::Image BackBuffer() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].first; }
	inline vk::Image BackBuffer(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].first; }
	inline vk::ImageView BackBufferView() const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[mBackBufferIndex].second; }
	inline vk::ImageView BackBufferView(uint32_t i) const { return mSwapchainImages.empty() ? nullptr : mSwapchainImages[i].second; }
	inline Semaphore& ImageAvailableSemaphore() const { return *mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }
	inline Device::QueueFamily* PresentQueueFamily() const { return mPresentQueueFamily; }

	inline bool Fullscreen() const { return mFullscreen; }
	inline string Title() const { return mTitle; }
	inline vk::Rect2D ClientRect() const { return mClientRect; };
	inline uint32_t BackBufferCount() const { return (uint32_t)mSwapchainImages.size(); }
	inline uint32_t BackBufferIndex() const { return mBackBufferIndex; }
	inline bool AllowTearing() const { return mAllowTearing; }

	#ifdef WIN32
	inline HWND Hwnd() const { return mHwnd; }
	#endif
	#ifdef __linux
	inline xcb_window_t XCBWindow() const { return mXCBWindow; }
	#endif

	STRATUM_API void AcquireNextImage();
	// Waits on all semaphores in waitSemaphores
	STRATUM_API void Present(const vector<vk::Semaphore>& waitSemaphores);


	STRATUM_API void LockMouse(bool l);
	inline bool LockMouse() const { return mLockMouse; }
	inline const stm::MouseState& MouseState() const { return mMouseState; }
	inline const stm::MouseState& LastMouseState() const { return mLastMouseState; }
	inline bool KeyDownFirst(KeyCode key) { return mMouseState.mKeys[key] && !mLastMouseState.mKeys[key]; }
	inline bool KeyUpFirst(KeyCode key) { return mLastMouseState.mKeys[key] && !mMouseState.mKeys[key]; }
	inline bool KeyDown(KeyCode key) { return mMouseState.mKeys[key]; }
	inline bool KeyUp(KeyCode key) { return !mMouseState.mKeys[key]; }
	inline float ScrollDelta() const { return mMouseState.mScrollDelta; }
	inline Vector2f CursorPos() const { return mMouseState.mCursorPos; }
	inline Vector2f LastCursorPos() const { return mLastMouseState.mCursorPos; }
	inline Vector2f CursorDelta() const { return mMouseState.mCursorDelta; }

private:
	friend class Instance;

	STRATUM_API void CreateSwapchain(Device& device);
	STRATUM_API void DestroySwapchain();
	
	vk::SurfaceKHR mSurface;
	Device* mSwapchainDevice = nullptr;
	Device::QueueFamily* mPresentQueueFamily = nullptr;
	vk::SwapchainKHR mSwapchain;
	vector<pair<vk::Image, vk::ImageView>> mSwapchainImages;
	vector<unique_ptr<Semaphore>> mImageAvailableSemaphores;
	vk::Extent2D mSwapchainExtent;
	vk::SurfaceFormatKHR mSurfaceFormat;
	uint32_t mBackBufferIndex = 0;
	uint32_t mImageAvailableSemaphoreIndex = 0;
	
	bool mFullscreen = false;
	bool mAllowTearing = false;
	vk::Rect2D mClientRect;
	string mTitle;

	stm::MouseState mMouseState = {};
	stm::MouseState mLastMouseState = {};
	bool mLockMouse = false;

	#ifdef WIN32
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
};

}