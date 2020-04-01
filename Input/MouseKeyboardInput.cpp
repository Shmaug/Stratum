#include <Input/MouseKeyboardInput.hpp>
#include <Core/Window.hpp>

MouseKeyboardInput::MouseKeyboardInput(){
	mMousePointer.mDevice = this;
	memset(mMousePointer.mAxis, 0, sizeof(float) * 5);
	memset(mMousePointer.mLastAxis, 0, sizeof(float) * 5);
	mLockMouse = false;
	mCurrent.mCursorPos = mLast.mCursorPos = 0;
	mCurrent.mCursorDelta = mLast.mCursorDelta = 0;
	mCurrent.mCursorDelta = mLast.mScrollDelta = 0;
	mCurrent.mKeys = {};
	mLast.mKeys = {};
}

void MouseKeyboardInput::LockMouse(bool l) {
	#ifdef WINDOWS
	if (mLockMouse && !l)
		ShowCursor(TRUE);
	else if (!mLockMouse && l)
		ShowCursor(FALSE);
	#else
	// TODO: hide cursor on linux
	#endif

	mLockMouse = l;
}

void MouseKeyboardInput::NextFrame() {
	mMousePointer.mLastWorldRay = mMousePointer.mWorldRay;
	memcpy(mMousePointer.mLastAxis, mMousePointer.mAxis, sizeof(float) * 5);
	mMousePointer.mLastGuiHitT = mMousePointer.mGuiHitT;
	mLast = mCurrent;
	mCurrent.mScrollDelta = 0;
	mCurrent.mCursorDelta = 0;
	mMousePointer.mGuiHitT = 1e20f;
}