#pragma once

#include "common.hpp"

namespace stm {

class Profiler {
public:
	struct sample_t {
		shared_ptr<sample_t> mParent;
		list<shared_ptr<sample_t>> mChildren;
		chrono::high_resolution_clock::time_point mStartTime;
		chrono::nanoseconds mDuration;
		float4 mColor;
		string mLabel;

		sample_t() = default;
		sample_t(sample_t&& s) = default;
		inline sample_t(const shared_ptr<sample_t>& parent, const string& label, const float4& color)
			: mParent(parent), mColor(color), mLabel(label), mStartTime(chrono::high_resolution_clock::now()), mDuration(chrono::nanoseconds::zero()) {}
	};

	inline static void begin_sample(const string& label, const float4& color = float4::Ones()) {
		auto s = make_shared<sample_t>(mCurrentSample, label, color);
		if (mCurrentSample)
			mCurrentSample = mCurrentSample->mChildren.emplace_back(s);
		else
			mCurrentSample = s;
	}
	inline static void end_sample() {
		if (!mCurrentSample) throw logic_error("cannot call end_sample without first calling begin_sample");
		mCurrentSample->mDuration += chrono::high_resolution_clock::now() - mCurrentSample->mStartTime;
		auto tmp = mCurrentSample;
		mCurrentSample = mCurrentSample->mParent;
		if (!mCurrentSample) {
			// frame ended (assume one ancestor sample per frame)
			if (mFrameTimeCount > 0) {
				mFrameTimes.emplace_back(chrono::duration_cast<chrono::duration<float, milli>>(tmp->mDuration).count());
				while (mFrameTimes.size() > mFrameTimeCount) mFrameTimes.pop_front();
			}
			if (mFrameHistoryCount > 0) {
				mFrameHistory.emplace_back(tmp);
				mFrameHistoryCount--;
			}
		}
	}

	inline static const auto& times() { return mFrameTimes; }
	inline static const auto& history() { return mFrameHistory; }
	inline static void clear_history() { mFrameHistory.clear(); }

	inline static void reset_timeline(uint32_t n) {
		if (mFrameHistory.empty())
			mFrameHistoryCount = n;
		else
			mFrameHistory.clear();
	}

	STRATUM_API static void timings_gui();
	STRATUM_API static void timeline_gui();

private:
	STRATUM_API static shared_ptr<sample_t> mCurrentSample;
	STRATUM_API static deque<float> mFrameTimes;
	STRATUM_API static uint32_t mFrameTimeCount;
	STRATUM_API static vector<shared_ptr<sample_t>> mFrameHistory;
	STRATUM_API static uint32_t mFrameHistoryCount;
};

}