#include <Util/Profiler.hpp>
#include <Scene/Scene.hpp>

#include <sstream>

using namespace std;
using namespace stm;

const std::chrono::high_resolution_clock Profiler::mTimer;
list<ProfilerSample*> Profiler::mFrames;
ProfilerSample* Profiler::mCurrentSample = nullptr;
ProfilerSample* Profiler::mSelectedFrame = nullptr;
uint32_t Profiler::mHistoryCount = 256;
bool Profiler::mEnabled = true;
float Profiler::mGraphHeight = 128;
float Profiler::mSampleHeight = 20;

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

	if (!mCurrentSample->mParent) throw logic_error("attempt to end nonexistant profiler sample!");
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
		fprintf_color(ConsoleColorBits::eYellow, stderr, "%s\n", "Warning: Profiler ProfilerSample %s was never ended!", mCurrentSample->mLabel.c_str());
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

void Profiler::DrawGui(GuiContext& gui, uint32_t framerate) {
	Device* device = gui.mDevice;
	auto font = device->LoadAsset<Font>("Assets/Fonts/OpenSans/OpenSans-Regular.ttf", "OpenSans-Regular");
	MouseKeyboardInput* input = gui.mInputManager->GetFirst<MouseKeyboardInput>();
	GuiContext::LayoutTheme theme = gui.mLayoutTheme;

	float toolbarHeight = 24;

	float4 graphBackgroundColor = gui.mLayoutTheme.mControlBackgroundColor;
	float4 graphTextColor = float4(0.8f, 0.8f, 0.8f, 1);
	float4 graphAxisColor = gui.mLayoutTheme.mSliderColor;
	float4 graphLineColor = float4(0.1f, 1.f, 0.2f, 1);
	float4 frameSelectLineColor = gui.mLayoutTheme.mSliderKnobColor;

	float2 s((float)input->WindowWidth(), (float)input->WindowHeight());
	float2 c = input->CursorPos();
	c.y = s.y - c.y;

	fRect2D windowRect(0, 0, s.x, toolbarHeight + mGraphHeight);

	gui.BeginScreenLayout(GuiContext::LayoutAxis::eVertical, windowRect);

	#pragma region toolbar
	gui.BeginSubLayout(GuiContext::LayoutAxis::eHorizontal, gui.mLayoutTheme.mControlSize + gui.mLayoutTheme.mControlPadding*2);
	gui.mLayoutTheme.mControlSize = 140;
	if (gui.LayoutTextButton(mEnabled ? "PAUSE PROFILER" : "RESUME PROFILER")) mEnabled = !mEnabled;
	gui.LayoutLabel(to_string(framerate) + " FPS", TextAnchor::eMax);
	gui.mLayoutTheme = theme;
	gui.EndLayout();
	#pragma endregion

	char buf[256];

	gui.BeginScrollSubLayout(windowRect.mSize.y - toolbarHeight, 256);

	fRect2D clipRect = gui.LayoutClipRect();
	float depth = gui.LayoutDepth() - 0.001f;

	gui.mLayoutTheme.mBackgroundColor.xyz *= 0.8f;
	fRect2D graphRect = gui.BeginSubLayout(GuiContext::LayoutAxis::eVertical, 128);
	gui.EndLayout();
	gui.mLayoutTheme = theme;

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
		gui.DrawString(graphRect.mOffset + float2(2, graphRect.mSize.y - 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		snprintf(buf, 256, "%ums", (uint32_t)graphWindowMin);
		gui.DrawString(graphRect.mOffset + float2(2, 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		// Horizontal graph lines
		for (uint32_t i = 1; i < 3; i++) {
			snprintf(buf, 256, "%.1fms", graphWindowMax * i / 3.f);
			gui.DrawString(graphRect.mOffset + float2(2, graphRect.mSize.y * (i / 3.f)), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
			gui.Rect(fRect2D(graphRect.mOffset.x + 32, graphRect.mOffset.y + graphRect.mSize.y * (i / 3.f) - 1, graphRect.mSize.x - 32, 1), depth, graphAxisColor, nullptr, 0, clipRect);
		}
		
		// Graph plot line
		for (uint32_t i = 0; i < pointCount; i++) {
			points[i].x = (float)i / ((float)pointCount - 1.f);
			points[i].y = (points[i].y - graphWindowMin) / (graphWindowMax - graphWindowMin);
		}
		gui.PolyLine(points, pointCount, graphLineColor, 1.25f, float3(graphRect.mOffset, 0), float3(graphRect.mSize, 1), clipRect);
		delete[] points;
	}

	gui.EndLayout(); // scroll
	
	gui.EndLayout(); // window
}