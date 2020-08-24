#include <Util/Profiler.hpp>
#include <Scene/Scene.hpp>

#include <sstream>

using namespace std;

const std::chrono::high_resolution_clock Profiler::mTimer;
list<ProfilerSample*> Profiler::mFrames;
ProfilerSample* Profiler::mCurrentSample = nullptr;
ProfilerSample* Profiler::mSelectedFrame = nullptr;
uint32_t Profiler::mHistoryCount = 256;
bool Profiler::mEnabled = true;
float Profiler::mGraphHeight = 128;
float Profiler::mSampleHeight = 20;

ProfilerSample::~ProfilerSample() {
	for (ProfilerSample* c : mChildren) delete c;
}

void Profiler::ClearAll() {
	safe_delete(mCurrentSample);
	for (ProfilerSample* frame : Profiler::mFrames) delete frame;
	Profiler::mFrames.swap(list<ProfilerSample*>());
}

void Profiler::BeginSample(const string& label) {
	if (!mCurrentSample) return;

	ProfilerSample* s = new ProfilerSample();
	s->mLabel = label;
	s->mParent = mCurrentSample;
	s->mStartTime = mTimer.now();
	s->mDuration = chrono::nanoseconds::zero();
	s->mColor = float4(.3f, .9f, .3f, 1);
	s->mChildren = {};
	mCurrentSample->mChildren.push_back(s);
	mCurrentSample =  s;
}
void Profiler::EndSample() {
	if (!mCurrentSample) return;

	if (!mCurrentSample->mParent) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "%s\n", "Error: Attempt to end nonexistant Profiler sample!");
		throw;
	}
	mCurrentSample->mDuration += mTimer.now() - mCurrentSample->mStartTime;
	mCurrentSample = mCurrentSample->mParent;
}

void Profiler::BeginFrame(uint64_t frameIndex) {
	if (!mEnabled) return;
	mCurrentSample = new ProfilerSample();
	mCurrentSample->mLabel, "Frame";
	mCurrentSample->mParent = nullptr;
	mCurrentSample->mStartTime = mTimer.now();
	mCurrentSample->mDuration = chrono::nanoseconds::zero();
	mCurrentSample->mChildren = {};
	mCurrentSample->mColor = float4(.15f, .6f, .15f, 1);
}
void Profiler::EndFrame() {
	if (!mCurrentSample) return;

	while (mCurrentSample->mParent) {
		fprintf_color(ConsoleColorBits::eYellow, stderr, "%s\n", "Warning: Profiler sample %s was never ended!", mCurrentSample->mLabel.c_str());
		mCurrentSample->mDuration += mTimer.now() - mCurrentSample->mStartTime;
		mCurrentSample = mCurrentSample->mParent;
	}

	mCurrentSample->mDuration = mTimer.now() - mCurrentSample->mStartTime;
	mFrames.push_front(mCurrentSample);
	mCurrentSample = nullptr;

	uint32_t i = 0;
	for (auto& it = mFrames.begin(); it != mFrames.end();) {
		if (i >= mHistoryCount) {
			if (mSelectedFrame == *it) mSelectedFrame = nullptr;
			delete *it;
			it = mFrames.erase(it);
		} else
			it++;
		i++;
	}
}

void Profiler::DrawGui(GuiContext* gui, uint32_t framerate) {
	Device* device = gui->Device();
	Font* font = device->AssetManager()->LoadFont("Assets/Fonts/OpenSans/OpenSans-Regular.ttf");
	MouseKeyboardInput* input = gui->InputManager()->GetFirst<MouseKeyboardInput>();
	GuiContext::LayoutTheme theme = gui->mLayoutTheme;

	char buf[256];

	float toolbarHeight = 24;

	float4 graphBackgroundColor = gui->mLayoutTheme.mControlBackgroundColor;
	float4 graphTextColor = float4(0.8f, 0.8f, 0.8f, 1);
	float4 graphAxisColor = gui->mLayoutTheme.mSliderColor;
	float4 graphLineColor = float4(0.1f, 1.f, 0.2f, 1);
	float4 frameSelectLineColor = gui->mLayoutTheme.mSliderKnobColor;

	float2 s((float)input->WindowWidth(), (float)input->WindowHeight());
	float2 c = input->CursorPos();
	c.y = s.y - c.y;

	fRect2D windowRect(0, 0, s.x, toolbarHeight + mGraphHeight);

	gui->BeginScreenLayout(LayoutAxis::eVertical, windowRect);

	#pragma region toolbar
	// Print memory allocations and descriptor set usage
	vk::DeviceSize memSize = 0;
	for (uint32_t i = 0; i < device->MemoryProperties().memoryHeapCount; i++)
		if (device->MemoryProperties().memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
			memSize += device->MemoryProperties().memoryHeaps[i].size;
	snprintf(buf, 256, "%u descriptor sets\t%u command buffers\t%u/%u allocations\t%.3f / %.3f mb (%.1f%%)\t %u FPS",
		device->DescriptorSetCount(),
		device->CommandBufferCount(),
		device->MemoryAllocationCount(), device->Limits().maxMemoryAllocationCount,
		device->MemoryUsage() / (1024.f * 1024.f), memSize / (1024.f * 1024.f), 100.f * (float)device->MemoryUsage() / (float)memSize,
		framerate );
	gui->LayoutLabel(buf, TextAnchor::eMax);
	
	gui->BeginSubLayout(LayoutAxis::eHorizontal, gui->mLayoutTheme.mControlSize + gui->mLayoutTheme.mControlPadding*2);
	gui->mLayoutTheme.mControlSize = 140;
	if (gui->LayoutTextButton(mEnabled ? "PAUSE PROFILER" : "RESUME PROFILER")) mEnabled = !mEnabled;
	gui->mLayoutTheme = theme;
	gui->EndLayout();

	#pragma endregion

	gui->BeginScrollSubLayout(windowRect.mExtent.y - toolbarHeight, 256);

	fRect2D clipRect = gui->LayoutClipRect();
	float depth = gui->LayoutDepth() - 0.001f;

	gui->mLayoutTheme.mBackgroundColor.rgb *= 0.8f;
	fRect2D graphRect = gui->BeginSubLayout(LayoutAxis::eVertical, 128);
	gui->EndLayout();
	gui->mLayoutTheme = theme;

	// Generate graph points and vertical selection lines
	if (mFrames.size()) {
		float3* points = new float3[mFrames.size()];
		memset(points, 0, sizeof(float3) * mFrames.size());

		float graphWindowMin = 30;
		float graphWindowMax = 0;
		uint32_t pointCount = 0;
		for (ProfilerSample* s : mFrames) {
			points[pointCount].y = s->mDuration.count() * 1e-6f;
			graphWindowMin = fminf(fmaxf(0, points[pointCount].y - 2), graphWindowMin);
			graphWindowMax = fmaxf(points[pointCount].y + 2, graphWindowMax);
			pointCount++;
		}
		
		graphWindowMin = floorf(graphWindowMin + 0.5f);
		graphWindowMax = floorf(graphWindowMax + 0.5f);
		// min/max graph labels
		snprintf(buf, 256, "%ums", (uint32_t)graphWindowMax);
		gui->DrawString(graphRect.mOffset + float2(2, graphRect.mExtent.y - 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		snprintf(buf, 256, "%ums", (uint32_t)graphWindowMin);
		gui->DrawString(graphRect.mOffset + float2(2, 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		// Horizontal graph lines
		for (uint32_t i = 1; i < 3; i++) {
			snprintf(buf, 256, "%.1fms", graphWindowMax * i / 3.f);
			gui->DrawString(graphRect.mOffset + float2(2, graphRect.mExtent.y * (i / 3.f)), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
			gui->Rect(fRect2D(graphRect.mOffset.x + 32, graphRect.mOffset.y + graphRect.mExtent.y * (i / 3.f) - 1, graphRect.mExtent.x - 32, 1), depth, graphAxisColor, nullptr, 0, clipRect);
		}
		
		// Graph plot line
		for (uint32_t i = 0; i < pointCount; i++) {
			points[i].x = (float)i / ((float)pointCount - 1.f);
			points[i].y = (points[i].y - graphWindowMin) / (graphWindowMax - graphWindowMin);
		}
		gui->PolyLine(points, pointCount, graphLineColor, 1.25f, graphRect.mOffset, graphRect.mExtent, clipRect);
		delete[] points;
	}

	gui->EndLayout(); // scroll
	
	gui->EndLayout(); // window
	
	return;
}