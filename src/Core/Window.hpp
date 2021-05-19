#pragma once

#include "CommandBuffer.hpp"
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
	eNone = 0x00,

	eMouse1 = VK_LBUTTON,
	eMouse2 = VK_RBUTTON,
	eMouse3 = VK_MBUTTON,
	eMouse4 = VK_XBUTTON1,
	eMouse5 = VK_XBUTTON2,

	eCancel = VK_CANCEL,
	
	eKeyBackspace = VK_BACK,
	eKeyTab = VK_TAB,
	eKeyEnter = VK_RETURN,
	eKeyCapsLock = VK_CAPITAL,
	eKeyEscape = VK_ESCAPE,

	eKeySpace = VK_SPACE,
	eKeyPageUp = VK_PRIOR,
	eKeyPageDown = VK_NEXT,
	eKeyEnd = VK_END,
	eKeyHome = VK_HOME,
	eKeyLeft = VK_LEFT,
	eKeyUp = VK_UP,
	eKeyRight = VK_RIGHT,
	eKeyDown = VK_DOWN,
	eKeyPrintScreen = VK_SNAPSHOT,
	eKeyInsert = VK_INSERT,
	eKeyDelete = VK_DELETE,
	eKey0 = 0x30,
	eKey1 = 0x31,
	eKey2 = 0x32,
	eKey3 = 0x33,
	eKey4 = 0x34,
	eKey5 = 0x35,
	eKey6 = 0x36,
	eKey7 = 0x37,
	eKey8 = 0x38,
	eKey9 = 0x39,
	eKeyA = 0x41,
	eKeyB = 0x42,
	eKeyC = 0x43,
	eKeyD = 0x44,
	eKeyE = 0x45,
	eKeyF = 0x46,
	eKeyG = 0x47,
	eKeyH = 0x48,
	eKeyI = 0x49,
	eKeyJ = 0x4a,
	eKeyK = 0x4b,
	eKeyL = 0x4c,
	eKeyM = 0x4d,
	eKeyN = 0x4e,
	eKeyO = 0x4f,
	eKeyP = 0x50,
	eKeyQ = 0x51,
	eKeyR = 0x52,
	eKeyS = 0x53,
	eKeyT = 0x54,
	eKeyU = 0x55,
	eKeyV = 0x56,
	eKeyW = 0x57,
	eKeyX = 0x58,
	eKeyY = 0x59,
	eKeyZ = 0x5a,
	eKeyNumpad0 = VK_NUMPAD0,
	eKeyNumpad1 = VK_NUMPAD1,
	eKeyNumpad2 = VK_NUMPAD2,
	eKeyNumpad3 = VK_NUMPAD3,
	eKeyNumpad4 = VK_NUMPAD4,
	eKeyNumpad5 = VK_NUMPAD5,
	eKeyNumpad6 = VK_NUMPAD6,
	eKeyNumpad7 = VK_NUMPAD7,
	eKeyNumpad8 = VK_NUMPAD8,
	eKeyNumpad9 = VK_NUMPAD9,
	eKeyNumpadEnter = VK_SEPARATOR,
	eKeyDecimal = VK_DECIMAL,
	eKeyAdd = VK_ADD,
	eKeySubtract = VK_SUBTRACT,
	eKeyMultiply = VK_MULTIPLY,
	eKeyDivide = VK_DIVIDE,
	eKeyF1  = VK_F1,
	eKeyF2  = VK_F2,
	eKeyF3  = VK_F3,
	eKeyF4  = VK_F4,
	eKeyF5  = VK_F5,
	eKeyF6  = VK_F6,
	eKeyF7  = VK_F7,
	eKeyF8  = VK_F8,
	eKeyF9  = VK_F9,
	eKeyF10 = VK_F10,
	eKeyF11 = VK_F11,
	eKeyF12 = VK_F12,
	eKeyNumLock = VK_NUMLOCK,
	eKeyScrollLock = VK_SCROLL,
	eKeyShift = VK_SHIFT,
	eKeyControl = VK_CONTROL,
	eKeyAlt = VK_MENU,

	eKeySemicolon = VK_OEM_1,
	eKeyEqual = VK_OEM_PLUS,
	eKeyComma = VK_OEM_COMMA,
	eKeyMinus = VK_OEM_MINUS,
	eKeyPeriod = VK_OEM_PERIOD,
	eKeySlash = VK_OEM_2,
	eKeyTilde = VK_OEM_3,
	eKeyLBracket = VK_OEM_4,
	eKeyRBracket = VK_OEM_6,
	eKeyQuote = VK_OEM_7,
	eKeyBackslash = VK_OEM_5,
#endif
#ifdef __linux
	eNone = 0x0000,

	eMouse1 = 0x001,
	eMouse2 = 0x002,
	eMouse3 = 0x003,
	eMouse4 = 0x004,
	eMouse5 = 0x005,
	
	eCancel = 0x006,

	eKeyBackspace = x11::XK_BackSpace,
	eKeyTab = x11::XK_Tab,
	eKeyEnter = x11::XK_Return,
	eKeyCapsLock = x11::XK_Caps_Lock,
	eKeyEscape = x11::XK_Escape,

	eKeySpace = x11::XK_space,
	eKeyPageUp = x11::XK_Page_Up,
	eKeyPageDown = x11::XK_Page_Down,
	eKeyEnd = x11::XK_End,
	eKeyHome = x11::XK_Home,
	eKeyLeft = x11::XK_Left,
	eKeyUp = x11::XK_Up,
	eKeyRight = x11::XK_Right,
	eKeyDown = x11::XK_Down,
	eKeyInsert = x11::XK_Insert,
	eKeyDelete = x11::XK_Delete,
	eKey0 = x11::XK_0,
	eKey1 = x11::XK_1,
	eKey2 = x11::XK_2,
	eKey3 = x11::XK_3,
	eKey4 = x11::XK_4,
	eKey5 = x11::XK_5,
	eKey6 = x11::XK_6,
	eKey7 = x11::XK_7,
	eKey8 = x11::XK_8,
	eKey9 = x11::XK_9,
	eKeyA = x11::XK_a,
	eKeyB = x11::XK_b,
	eKeyC = x11::XK_c,
	eKeyD = x11::XK_d,
	eKeyE = x11::XK_e,
	eKeyF = x11::XK_f,
	eKeyG = x11::XK_g,
	eKeyH = x11::XK_h,
	eKeyI = x11::XK_i,
	eKeyJ = x11::XK_j,
	eKeyK = x11::XK_k,
	eKeyL = x11::XK_l,
	eKeyM = x11::XK_m,
	eKeyN = x11::XK_n,
	eKeyO = x11::XK_o,
	eKeyP = x11::XK_p,
	eKeyQ = x11::XK_q,
	eKeyR = x11::XK_r,
	eKeyS = x11::XK_s,
	eKeyT = x11::XK_t,
	eKeyU = x11::XK_u,
	eKeyV = x11::XK_v,
	eKeyW = x11::XK_w,
	eKeyX = x11::XK_x,
	eKeyY = x11::XK_y,
	eKeyZ = x11::XK_z,
	eKeyNumpad0 = x11::XK_KP_Insert,
	eKeyNumpad1 = x11::XK_KP_End,
	eKeyNumpad2 = x11::XK_KP_Down,
	eKeyNumpad3 = x11::XK_KP_Page_Down,
	eKeyNumpad4 = x11::XK_KP_Left,
	eKeyNumpad5 = x11::XK_KP_Begin,
	eKeyNumpad6 = x11::XK_KP_Right,
	eKeyNumpad7 = x11::XK_KP_Home,
	eKeyNumpad8 = x11::XK_KP_Up,
	eKeyNumpad9 = x11::XK_KP_Page_Up,
	eKeyNumpadEnter = x11::XK_KP_Enter,
	eKeyDecimal = x11::XK_KP_Delete,
	eKeyAdd = x11::XK_KP_Add,
	eKeySubtract = x11::XK_KP_Subtract,
	eKeyMultiply = x11::XK_KP_Multiply,
	eKeyDivide = x11::XK_KP_Divide,
	eKeyF1 = x11::XK_F1,
	eKeyF2 = x11::XK_F2,
	eKeyF3 = x11::XK_F3,
	eKeyF4 = x11::XK_F4,
	eKeyF5 = x11::XK_F5,
	eKeyF6 = x11::XK_F6,
	eKeyF7 = x11::XK_F7,
	eKeyF8 = x11::XK_F8,
	eKeyF9 = x11::XK_F9,
	eKeyF10 = x11::XK_F10,
	eKeyF11 = x11::XK_F11,
	eKeyF12 = x11::XK_F12,
	eKeyNumLock = x11::XK_Num_Lock,
	eKeyScrollLock = x11::XK_Scroll_Lock,
	eKeyLShift = x11::XK_Shift_L,
	eKeyRShift = x11::XK_Shift_R,
	eKeyLControl = x11::XK_Control_L,
	eKeyRControl = x11::XK_Control_R,
	eKeyLAlt = x11::XK_Alt_L,
	eKeyRAlt = x11::XK_Alt_R,

	eKeySemicolon = x11::XK_semicolon,
	eKeyEqual = x11::XK_equal,
	eKeyComma = x11::XK_comma,
	eKeyMinus = x11::XK_minus,
	eKeyPeriod = x11::XK_period,
	eKeySlash = x11::XK_slash,
	eKeyTilde = x11::XK_grave,
	eKeyLBracket = x11::XK_bracketleft,
	eKeyRBracket = x11::XK_bracketright,
	eKeyQuote = x11::XK_quotedbl,
	eKeyBackslash = x11::XK_backslash,
#endif
};

class MouseKeyboardState : public InputState {
private:
	Vector2f mCursorPos;
	Vector2f mCursorDelta;
	float mScrollDelta;
	unordered_set<KeyCode> mButtons;
public:
	MouseKeyboardState(const MouseKeyboardState& ms) = default;
	MouseKeyboardState(MouseKeyboardState&& ms) = default;
	inline MouseKeyboardState() : InputState("MouseKeyboard") {};
	inline MouseKeyboardState& operator=(const MouseKeyboardState& rhs) {
		mCursorPos = rhs.mCursorPos;
		mCursorDelta = rhs.mCursorDelta;
		mScrollDelta = rhs.mScrollDelta;
		mButtons = rhs.mButtons;
		return *this;
	};
	
	inline void clear_deltas() {
		mCursorDelta = Vector2f::Zero();
		mScrollDelta = 0;
	}
	inline void add_cursor_delta(const Vector2f& delta) {
		mCursorDelta += delta;
	}
	inline void add_scroll_delta(float delta) {
		mScrollDelta += delta;
	}
	inline Vector2f& cursor_pos() { return mCursorPos; }

	inline const Vector2f& cursor_pos() const { return mCursorPos; }
	inline const Vector2f& cursor_delta() const { return mCursorDelta; }
	inline float scroll_delta() const { return mScrollDelta; }
	inline bool pressed(KeyCode key) const {
		return mButtons.count(key);
	}
	inline void set_button(KeyCode key) {
		mButtons.emplace(key);
	}
	inline void unset_button(KeyCode key) {
		mButtons.erase(key);
	}
};

class Window {
public:
	Instance& mInstance;
	
	STRATUM_API Window(Instance& instance, const string& title, vk::Rect2D position);
	STRATUM_API ~Window();

	inline const string& title() const { return mTitle; }
	inline const vk::Rect2D& client_rect() const { return mClientRect; };
	inline vk::SurfaceKHR surface() const { return mSurface; }
	inline vk::SwapchainKHR swapchain() const { return mSwapchain; }
	inline vk::SurfaceFormatKHR surface_format() const { return mSurfaceFormat; }
	inline vk::Extent2D swapchain_extent() const { return mSwapchainExtent; }
	inline Semaphore& image_available_semaphore() const { return *mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }
	inline Device::QueueFamily* present_queue_family() const { return mPresentQueueFamily; }
	
	inline void allow_tearing(bool v) { mAllowTearing = v; }
	inline bool allow_tearing() const { return mAllowTearing; }

	STRATUM_API void fullscreen(bool fs);
	inline bool fullscreen() const { return mFullscreen; }

	inline uint32_t back_buffer_count() const { return (uint32_t)mSwapchainImages.size(); }
	inline uint32_t back_buffer_index() const { return mBackBufferIndex; }
	inline const Texture::View& back_buffer() const { return mSwapchainImages[back_buffer_index()]; }
	inline const Texture::View& back_buffer(uint32_t i) const { return mSwapchainImages[i]; }


#ifdef WIN32
	inline HWND handle() const { return mHwnd; }
#endif
#ifdef __linux
	inline xcb_window_t handle() const { return mXCBWindow; }
#endif

	STRATUM_API void resize(uint32_t w, uint32_t h);

	STRATUM_API const Texture::View& acquire_image(CommandBuffer& commandBuffer);
	// Waits on all semaphores in waitSemaphores
	STRATUM_API void present(const vector<vk::Semaphore>& waitSemaphores);
	// Number of times present has been called
	inline size_t present_count() const { return mPresentCount; }

	STRATUM_API void lock_mouse(bool l);
	inline bool lock_mouse() const { return mLockMouse; }
	inline const MouseKeyboardState& input_state() const { return mInputState; }
	inline const MouseKeyboardState& input_state_last() const { return mInputStateLast; }
	// position reported by the OS
	inline const Vector2f& cursor_pos() const { return mInputState.cursor_pos(); }
	inline const Vector2f& cursor_pos_last() const { return mInputStateLast.cursor_pos(); }
	// Note that cursor_delta != cursor_pos_last - cursor_pos, as cursor_delta comes from raw input
	inline const Vector2f& cursor_delta() const { return mInputState.cursor_delta(); }
	inline float scroll_delta() const { return mInputState.scroll_delta(); }
	inline bool pressed(KeyCode key) const { return mInputState.pressed(key); }
	inline bool released(KeyCode key) const { return !mInputState.pressed(key); }
	inline bool pressed_redge(KeyCode key) const { return mInputState.pressed(key) && !mInputStateLast.pressed(key); }
	inline bool released_redge(KeyCode key) const { return mInputStateLast.pressed(key) && !mInputState.pressed(key); }

	inline Vector2f clip_to_window(const Vector2f& clip) const {
		return (clip.array()*.5f + Array2f::Constant(.5f))*Array2f((float)mSwapchainExtent.width, -(float)mSwapchainExtent.height);
	}
	inline Vector2f window_to_clip(const Vector2f& screen) const {
		Vector2f r = screen.array()/Array2f((float)mSwapchainExtent.width, (float)mSwapchainExtent.height)*2 - Array2f::Ones();
		r.y() = -r.y();
		return r;
	}

private:
	STRATUM_API void create_swapchain(CommandBuffer& commandBUffer);
	STRATUM_API void destroy_swapchain();
	
	vk::SurfaceKHR mSurface;
	Device* mSwapchainDevice = nullptr;
	Device::QueueFamily* mPresentQueueFamily = nullptr;
	vk::SwapchainKHR mSwapchain;
	vector<Texture::View> mSwapchainImages;
	vector<unique_ptr<Semaphore>> mImageAvailableSemaphores;
	vk::Extent2D mSwapchainExtent;
	vk::SurfaceFormatKHR mSurfaceFormat;
	uint32_t mBackBufferIndex = 0;
	uint32_t mImageAvailableSemaphoreIndex = 0;
	size_t mPresentCount = 0;
	
	bool mFullscreen = false;
	bool mAllowTearing = false;
	vk::Rect2D mClientRect;
	string mTitle;

	MouseKeyboardState mInputState;
	MouseKeyboardState mInputStateLast;
	bool mLockMouse = false;

#ifdef WIN32
	friend class Instance;
	HWND mHwnd = NULL;
	RECT mWindowedRect;
	STRATUM_API void handle_message(UINT message, WPARAM wParam, LPARAM lParam);
#endif
#ifdef __linux
	xcb_window_t mXCBWindow;
	xcb_atom_t mXCBProtocols;
	xcb_atom_t mXCBDeleteWin;
	vk::Rect2D mWindowedRect;
	STRATUM_API void process_event(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* poll_event();
#endif
};

}