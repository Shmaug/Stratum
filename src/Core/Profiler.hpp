#pragma once

#include <Common/common.hpp>

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
		if (!mCurrentSample->mParent && mSampleHistory.size() < mSampleHistoryCount)
			mSampleHistory.emplace_back(mCurrentSample);
		mCurrentSample = mCurrentSample->mParent;
	}

	inline static void set_timestamps(const chrono::steady_clock::time_point& t0, const vector<pair<string,chrono::nanoseconds>>& gpuTimestamps) {
		if (gpuTimestamps.size() && mTimestamps.size() < mSampleHistoryCount)
			mTimestamps.emplace_back(t0, gpuTimestamps);
	}
	inline static void begin_frame() {
		auto rn = chrono::high_resolution_clock::now();
		if (mFrameStart && mFrameTimeCount > 0) {
			auto duration = rn - *mFrameStart;
			mFrameTimes.emplace_back(chrono::duration_cast<chrono::duration<float, milli>>(duration).count());
			while (mFrameTimes.size() > mFrameTimeCount) mFrameTimes.pop_front();
		}
		mFrameStart = rn;
	}

	inline static bool has_history() { return !mSampleHistory.empty(); }
	inline static void reset_history(uint32_t n) {
		mSampleHistoryCount = n;
		mSampleHistory.clear();
		mTimestamps.clear();
	}

	STRATUM_API static void frame_times_gui();
	STRATUM_API static void sample_timeline_gui();
	STRATUM_API static void gpu_timestamp_gui();

private:
	STRATUM_API static shared_ptr<sample_t> mCurrentSample;
	STRATUM_API static vector<pair<chrono::steady_clock::time_point, vector<pair<string,chrono::nanoseconds>>>> mTimestamps;
	STRATUM_API static vector<shared_ptr<sample_t>> mSampleHistory;
	STRATUM_API static uint32_t mSampleHistoryCount;
	STRATUM_API static optional<chrono::high_resolution_clock::time_point> mFrameStart;
	STRATUM_API static deque<float> mFrameTimes;
	STRATUM_API static uint32_t mFrameTimeCount;
};

}