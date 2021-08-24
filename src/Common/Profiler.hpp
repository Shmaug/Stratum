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
		Vector4f mColor;
		string mLabel;
		
		sample_t() = default;
		sample_t(sample_t&& s) = default;
		inline sample_t(const shared_ptr<sample_t>& parent, const string& label, const Vector4f& color)
			: mParent(parent), mColor(color), mLabel(label), mStartTime(chrono::high_resolution_clock::now()), mDuration(chrono::nanoseconds::zero()) {}
	};

	inline static void begin_sample(const string& label, const Vector4f& color = Vector4f(.3f, .9f, .3f, 1)) {
		auto s = make_unique<sample_t>(mCurrentSample, label, color);
		if (mCurrentSample)
			mCurrentSample = mCurrentSample->mChildren.emplace_back(move(s));
		else {
			mCurrentSample = mFrameHistory.emplace_front(move(s));
			while (mFrameHistory.size() > mFrameHistoryCount) mFrameHistory.pop_back();
		}
	}
	inline static shared_ptr<sample_t> end_sample() {
		if (!mCurrentSample) throw logic_error("cannot call end_sample without first calling begin_sample");
		mCurrentSample->mDuration += chrono::high_resolution_clock::now() - mCurrentSample->mStartTime;
		auto tmp = mCurrentSample;
		mCurrentSample = mCurrentSample->mParent;
		return tmp;
	}

	inline static const auto& history() { return mFrameHistory; }
	inline static void clear_history() { mFrameHistory.clear(); }

	STRATUM_API static void on_gui();

private:
	STRATUM_API static size_t mFrameHistoryCount;
	STRATUM_API static list<shared_ptr<sample_t>> mFrameHistory;
	STRATUM_API static shared_ptr<sample_t> mCurrentSample;
};

}