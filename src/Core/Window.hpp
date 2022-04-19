#pragma once

#include "CommandBuffer.hpp"

namespace stm {

enum KeyCode {
#ifdef _WIN32
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

	eKeyBackspace = XK_BackSpace,
	eKeyTab = XK_Tab,
	eKeyEnter = XK_Return,
	eKeyCapsLock = XK_Caps_Lock,
	eKeyEscape = XK_Escape,

	eKeySpace = XK_space,
	eKeyPageUp = XK_Page_Up,
	eKeyPageDown = XK_Page_Down,
	eKeyEnd = XK_End,
	eKeyHome = XK_Home,
	eKeyLeft = XK_Left,
	eKeyUp = XK_Up,
	eKeyRight = XK_Right,
	eKeyDown = XK_Down,
	eKeyInsert = XK_Insert,
	eKeyDelete = XK_Delete,
	eKey0 = XK_0,
	eKey1 = XK_1,
	eKey2 = XK_2,
	eKey3 = XK_3,
	eKey4 = XK_4,
	eKey5 = XK_5,
	eKey6 = XK_6,
	eKey7 = XK_7,
	eKey8 = XK_8,
	eKey9 = XK_9,
	eKeyA = XK_a,
	eKeyB = XK_b,
	eKeyC = XK_c,
	eKeyD = XK_d,
	eKeyE = XK_e,
	eKeyF = XK_f,
	eKeyG = XK_g,
	eKeyH = XK_h,
	eKeyI = XK_i,
	eKeyJ = XK_j,
	eKeyK = XK_k,
	eKeyL = XK_l,
	eKeyM = XK_m,
	eKeyN = XK_n,
	eKeyO = XK_o,
	eKeyP = XK_p,
	eKeyQ = XK_q,
	eKeyR = XK_r,
	eKeyS = XK_s,
	eKeyT = XK_t,
	eKeyU = XK_u,
	eKeyV = XK_v,
	eKeyW = XK_w,
	eKeyX = XK_x,
	eKeyY = XK_y,
	eKeyZ = XK_z,
	eKeyNumpad0 = XK_KP_Insert,
	eKeyNumpad1 = XK_KP_End,
	eKeyNumpad2 = XK_KP_Down,
	eKeyNumpad3 = XK_KP_Page_Down,
	eKeyNumpad4 = XK_KP_Left,
	eKeyNumpad5 = XK_KP_Begin,
	eKeyNumpad6 = XK_KP_Right,
	eKeyNumpad7 = XK_KP_Home,
	eKeyNumpad8 = XK_KP_Up,
	eKeyNumpad9 = XK_KP_Page_Up,
	eKeyNumpadEnter = XK_KP_Enter,
	eKeyDecimal = XK_KP_Delete,
	eKeyAdd = XK_KP_Add,
	eKeySubtract = XK_KP_Subtract,
	eKeyMultiply = XK_KP_Multiply,
	eKeyDivide = XK_KP_Divide,
	eKeyF1 = XK_F1,
	eKeyF2 = XK_F2,
	eKeyF3 = XK_F3,
	eKeyF4 = XK_F4,
	eKeyF5 = XK_F5,
	eKeyF6 = XK_F6,
	eKeyF7 = XK_F7,
	eKeyF8 = XK_F8,
	eKeyF9 = XK_F9,
	eKeyF10 = XK_F10,
	eKeyF11 = XK_F11,
	eKeyF12 = XK_F12,
	eKeyNumLock = XK_Num_Lock,
	eKeyScrollLock = XK_Scroll_Lock,
	eKeyShift = XK_Shift_L,
	eKeyControl = XK_Control_L,
	eKeyAlt = XK_Alt_L,

	eKeySemicolon = XK_semicolon,
	eKeyEqual = XK_equal,
	eKeyComma = XK_comma,
	eKeyMinus = XK_minus,
	eKeyPeriod = XK_period,
	eKeySlash = XK_slash,
	eKeyTilde = XK_grave,
	eKeyLBracket = XK_bracketleft,
	eKeyRBracket = XK_bracketright,
	eKeyQuote = XK_quotedbl,
	eKeyBackslash = XK_backslash,
#endif
};

class MouseKeyboardState {
public:
	inline Array2f& cursor_pos() { return mCursorPos; }
	inline const unordered_set<KeyCode>& buttons() const { return mButtons; }
	inline const Array2f& cursor_pos() const { return mCursorPos; }
	inline const Array2f& cursor_delta() const { return mCursorDelta; }
	inline float scroll_delta() const { return mScrollDelta; }
	inline const string& input_characters() const { return mInputCharacters; };
	inline bool pressed(KeyCode key) const { return mButtons.count(key); }
	inline const vector<string>& files() const { return mInputFiles; }

private:
	friend class Instance;
	friend class Window;

	Array2f mCursorPos;
	Array2f mCursorDelta;
	float mScrollDelta;
	unordered_set<KeyCode> mButtons;
	string mInputCharacters;
	vector<string> mInputFiles;
	
	inline void clear() {
		mCursorDelta = Array2f::Zero();
		mScrollDelta = 0;
		mInputCharacters.clear();
		mInputFiles.clear();
	}
	inline void add_cursor_delta(const Array2f& delta) { mCursorDelta += delta; }
	inline void add_scroll_delta(float delta) { mScrollDelta += delta; }
	inline void add_input_character(char c) { mInputCharacters.push_back(c); };
	inline void set_button(KeyCode key) { mButtons.emplace(key); }
	inline void unset_button(KeyCode key) { mButtons.erase(key); }

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
	inline vk::PresentModeKHR present_mode() const { return mPresentMode; }
	inline const vk::Extent2D& swapchain_extent() const { return mSwapchainExtent; }
	inline const shared_ptr<Semaphore>& image_available_semaphore() const { return mImageAvailableSemaphores[mImageAvailableSemaphoreIndex]; }
	inline Device::QueueFamily* present_queue_family() const { return mPresentQueueFamily; }
	
	inline void acquire_image_timeout(const chrono::nanoseconds& v) { mAcquireImageTimeout = v; }
	inline const chrono::nanoseconds& acquire_image_timeout() const { return mAcquireImageTimeout; }

	inline void preferred_present_mode(vk::PresentModeKHR v) { if (mPreferredPresentMode != v) mRecreateSwapchain = true; mPreferredPresentMode = v; }
	inline vk::PresentModeKHR preferred_present_mode() const { return mPreferredPresentMode; }

	STRATUM_API void fullscreen(bool fs);
	inline bool fullscreen() const { return mFullscreen; }

	inline uint32_t back_buffer_count() const { return (uint32_t)mSwapchainImages.size(); }
	inline uint32_t back_buffer_index() const { return mBackBufferIndex; }
	inline const Image::View& back_buffer() const { return mRenderTargets[back_buffer_index()]; }
	inline const Image::View& back_buffer(uint32_t i) const { return mRenderTargets[i]; }

#ifdef _WIN32
	inline HWND handle() const { return mHwnd; }
#endif
#ifdef __linux
	inline xcb_window_t handle() const { return mXCBWindow; }
#endif

	STRATUM_API void resize(uint32_t w, uint32_t h);

	STRATUM_API bool acquire_image();
	STRATUM_API void resolve(CommandBuffer& commandBuffer);
	// Waits on all semaphores in waitSemaphores
	STRATUM_API void present(const vk::ArrayProxyNoTemporaries<const vk::Semaphore>& waitSemaphores = {});
	// Number of times present has been called
	inline size_t present_count() const { return mPresentCount; }

	inline const MouseKeyboardState& input_state() const { return mInputState; }
	inline const MouseKeyboardState& input_state_last() const { return mInputStateLast; }

	inline Array2f clip_to_window(const Array2f& clip) const {
		return (clip*.5f + Array2f::Constant(.5f))*Array2f((float)mSwapchainExtent.width, -(float)mSwapchainExtent.height);
	}
	inline Array2f window_to_clip(const Array2f& screen) const {
		Array2f r = screen/Array2f((float)mSwapchainExtent.width, (float)mSwapchainExtent.height)*2 - 1;
		r.y() = -r.y();
		return r;
	}

private:
	STRATUM_API void create_swapchain();
	STRATUM_API void destroy_swapchain();
	
	vk::SurfaceKHR mSurface;
	Device::QueueFamily* mPresentQueueFamily = nullptr;
	vk::SwapchainKHR mSwapchain;
	vector<Image::View> mRenderTargets;
	vector<Image::View> mSwapchainImages;
	vector<shared_ptr<Semaphore>> mImageAvailableSemaphores;
	vk::Extent2D mSwapchainExtent;
	vk::SurfaceFormatKHR mSurfaceFormat;
	vk::PresentModeKHR mPresentMode;
	uint32_t mBackBufferIndex = 0;
	uint32_t mImageAvailableSemaphoreIndex = 0;
	size_t mPresentCount = 0;

	vk::PresentModeKHR mPreferredPresentMode = vk::PresentModeKHR::eMailbox;
	chrono::nanoseconds mAcquireImageTimeout = 10s;
	
	bool mFullscreen = false;
	bool mRecreateSwapchain = false;
	vk::Rect2D mClientRect;
	string mTitle;

	MouseKeyboardState mInputState;
	MouseKeyboardState mInputStateLast;

	friend class Instance;
#ifdef _WIN32
	HWND mHwnd;
	RECT mWindowedRect;
	STRATUM_API void handle_message(UINT message, WPARAM wParam, LPARAM lParam);
#endif
#ifdef __linux
	xcb_window_t mXCBWindow;
	xcb_atom_t mXCBProtocols;
	xcb_atom_t mXCBDeleteWin;
	vk::Rect2D mWindowedRect;
	STRATUM_API bool process_event(xcb_generic_event_t* event);
	STRATUM_API xcb_generic_event_t* poll_event();
#endif
};

}