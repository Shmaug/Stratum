#pragma once

#define PROFILER_ENABLE

#ifdef PROFILER_ENABLE
#define PROFILER_BEGIN(label) Profiler::BeginSample(label);
#define PROFILER_END Profiler::EndSample();
#else
#define PROFILER_BEGIN(label) 
#define PROFILER_END
#endif

#include <Util/Util.hpp>

class Stratum;
class GuiContext;

struct ProfilerSample {
	std::string mLabel;
	ProfilerSample* mParent;
	std::list<ProfilerSample*> mChildren;
	std::chrono::high_resolution_clock::time_point mStartTime;
	std::chrono::nanoseconds mDuration;
	float4 mColor;
	
	ENGINE_EXPORT ~ProfilerSample();
};

class Profiler {
public:
	ENGINE_EXPORT static void BeginSample(const std::string& label);
	ENGINE_EXPORT static void EndSample();

	ENGINE_EXPORT static void DrawProfiler(GuiContext* gui);

private:
	friend class Stratum;
	ENGINE_EXPORT static void FrameStart(uint64_t frameIndex);
	ENGINE_EXPORT static void FrameEnd();
	ENGINE_EXPORT static void Destroy();

	ENGINE_EXPORT static bool mEnabled;
	ENGINE_EXPORT static std::list<ProfilerSample*> mFrames;
	ENGINE_EXPORT static ProfilerSample* mCurrentSample;
	ENGINE_EXPORT static uint32_t mHistoryCount;
	ENGINE_EXPORT static const std::chrono::high_resolution_clock mTimer;

	// Drawing settings
	
	ENGINE_EXPORT static ProfilerSample* mSelectedFrame;
	ENGINE_EXPORT static float mGraphHeight;
	ENGINE_EXPORT static float mSampleHeight;
};