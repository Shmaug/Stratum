#pragma once

#include "../Stratum.hpp"

namespace stm {

struct ProfilerSample {
	string mLabel;
	ProfilerSample* mParent;
	list<ProfilerSample*> mChildren;
	chrono::high_resolution_clock::time_point mStartTime;
	chrono::nanoseconds mDuration;
	Vector4f mColor;
	inline ~ProfilerSample() { for (ProfilerSample* c : mChildren) delete c; }
};

class Profiler {
public:
	STRATUM_API static void BeginSample(const string& label);
	STRATUM_API static void EndSample();

	STRATUM_API static void ClearAll();

private:
	friend class Instance;
	STRATUM_API static void BeginFrame(uint64_t frameIndex);
	STRATUM_API static void EndFrame();

	STRATUM_API static bool mEnabled;
	STRATUM_API static list<ProfilerSample*> mFrames;
	STRATUM_API static ProfilerSample* mCurrentSample;
	STRATUM_API static uint32_t mHistoryCount;
	STRATUM_API static const chrono::high_resolution_clock mTimer;

	// Drawing settings
	
	STRATUM_API static ProfilerSample* mSelectedFrame;
	STRATUM_API static float mGraphHeight;
	STRATUM_API static float mSampleHeight;
};

}