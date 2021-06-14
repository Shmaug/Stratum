#pragma once

#include "common.hpp"

namespace stm {

struct ProfilerSample {
	ProfilerSample* mParent;
	list<unique_ptr<ProfilerSample>> mChildren;
	chrono::high_resolution_clock::time_point mStartTime;
	chrono::nanoseconds mDuration;
	Vector4f mColor;
	string mLabel;
};

class Profiler {
private:
	STRATUM_API static list<ProfilerSample> mFrameHistory;
	STRATUM_API static ProfilerSample* mCurrentSample;
	STRATUM_API static size_t mHistoryCount;
	STRATUM_API static bool mPaused;
	STRATUM_API static unique_ptr<ProfilerSample> mTimelineSample;

public:

	inline static void begin_sample(const string& label, const Vector4f& color = Vector4f(.3f, .9f, .3f, 1)) {
		ProfilerSample s;
		s.mParent = mCurrentSample;
		s.mStartTime = chrono::high_resolution_clock::now();
		s.mDuration = chrono::nanoseconds::zero();
		s.mLabel = label;
		s.mColor = color;
		if (mCurrentSample)
			mCurrentSample = mCurrentSample->mChildren.emplace_back(make_unique<ProfilerSample>(move(s))).get();
		else {
			mCurrentSample = &mFrameHistory.emplace_front(move(s));
			while (mFrameHistory.size() > mHistoryCount)
				mFrameHistory.pop_back();
		}
	}
	inline static ProfilerSample& end_sample() {
		if (!mCurrentSample) throw logic_error("attempt to end nonexistant profiler sample!");
		if (mPaused) return *mCurrentSample;
		mCurrentSample->mDuration += chrono::high_resolution_clock::now() - mCurrentSample->mStartTime;
		ProfilerSample* tmp = mCurrentSample;
		mCurrentSample = mCurrentSample->mParent;
		return *tmp;
	}

	inline static const list<ProfilerSample>& history() { return mFrameHistory; }
	inline static void clear() { mFrameHistory.clear(); }

	static void draw_imgui();
};

}