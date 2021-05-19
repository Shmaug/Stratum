#include "Profiler.hpp"

using namespace stm;

list<ProfilerSample> Profiler::mFrameHistory;
ProfilerSample* Profiler::mCurrentSample = nullptr;
size_t Profiler::mHistoryCount = 256;