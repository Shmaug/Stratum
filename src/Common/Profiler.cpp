#include "Profiler.hpp"

using namespace stm;

list<shared_ptr<Profiler::sample_t>> Profiler::mFrameHistory;
shared_ptr<Profiler::sample_t> Profiler::mCurrentSample;
size_t Profiler::mFrameHistoryCount = 256;