#include <Util/Profiler.hpp>
#include <Content/Font.hpp>
#include <Scene/GUI.hpp>
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

void Profiler::Destroy() {
	for (ProfilerSample* frame : Profiler::mFrames)
		delete frame;
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
		fprintf_color(COLOR_RED, stderr, "%s\n", "Error: Attempt to end nonexistant Profiler sample!");
		throw;
	}
	mCurrentSample->mDuration += mTimer.now() - mCurrentSample->mStartTime;
	mCurrentSample = mCurrentSample->mParent;
}

void Profiler::FrameStart(uint64_t frameIndex) {
	if (!mEnabled) return;

	mCurrentSample = new ProfilerSample();
	mCurrentSample->mLabel, "Frame";
	mCurrentSample->mParent = nullptr;
	mCurrentSample->mStartTime = mTimer.now();
	mCurrentSample->mDuration = chrono::nanoseconds::zero();
	mCurrentSample->mChildren = {};
	mCurrentSample->mColor = float4(.15f, .6f, .15f, 1);
}
void Profiler::FrameEnd() {
	if (!mCurrentSample) return;

	while (mCurrentSample->mParent) {
		fprintf_color(COLOR_YELLOW, stderr, "%s\n", "Warning: Profiler sample %s was never ended!", mCurrentSample->mLabel.c_str());
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

void Profiler::DrawProfiler(Scene* scene) {
	Device* device = scene->Instance()->Device();
	Font* font = device->AssetManager()->LoadFont("Assets/Fonts/FantasqueSansMono/FantasqueSansMono-Regular.ttf", 12);
	MouseKeyboardInput* input = scene->InputManager()->GetFirst<MouseKeyboardInput>();
	GUI::LayoutTheme theme = GUI::mLayoutTheme;

	float toolbarHeight = 18;

	float4 graphBackgroundColor = GUI::mLayoutTheme.mControlBackgroundColor;
	float4 graphTextColor = float4(0.8f, 0.8f, 0.8f, 1);
	float4 graphAxisColor = GUI::mLayoutTheme.mSliderColor;
	float4 graphLineColor = float4(0.1f, 1.f, 0.2f, 1);
	float4 frameSelectLineColor = GUI::mLayoutTheme.mSliderKnobColor;

	float2 s(input->WindowWidth(), input->WindowHeight());
	float2 c = input->CursorPos();
	c.y = s.y - c.y;

	fRect2D windowRect(0, 0, s.x, toolbarHeight + mGraphHeight);

	GUI::BeginScreenLayout(LAYOUT_VERTICAL, windowRect);

	#pragma region toolbar
	GUI::BeginSubLayout(LAYOUT_HORIZONTAL, toolbarHeight);
	
	GUI::mLayoutTheme.mControlSize = 100;
	if (GUI::LayoutTextButton(mEnabled ? "PAUSE PROFILER" : "RESUME PROFILER"))
		mEnabled = !mEnabled;

	GUI::mLayoutTheme.mControlSize = windowRect.mExtent.x - 120;

	// Print memory allocations and descriptor set usage
	VkDeviceSize memSize = 0;
	for (uint32_t i = 0; i < device->MemoryProperties().memoryHeapCount; i++)
		if (device->MemoryProperties().memoryHeaps[i].flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			memSize += device->MemoryProperties().memoryHeaps[i].size;
	char buf[128];
	snprintf(buf, 128, "%.1f fps\t%u descriptor sets\t%u command buffers\t%u/%u allocations\t%.3f / %.3f mb (%.1f%%)",
		scene->FPS(),
		scene->Instance()->Device()->DescriptorSetCount(),
		scene->Instance()->Device()->CommandBufferCount(),
		device->MemoryAllocationCount(), device->Limits().maxMemoryAllocationCount,
		device->MemoryUsage() / (1024.f * 1024.f), memSize / (1024.f * 1024.f), 100.f * (float)device->MemoryUsage() / (float)memSize );
	GUI::LayoutLabel(buf, TEXT_ANCHOR_MAX);

	GUI::mLayoutTheme = theme;

	GUI::EndLayout();
	#pragma endregion

	GUI::BeginScrollSubLayout(windowRect.mExtent.y - toolbarHeight, 256);

	fRect2D clipRect = GUI::LayoutClipRect();
	float depth = GUI::LayoutDepth() - 0.001f;

	GUI::mLayoutTheme.mBackgroundColor.rgb *= 0.8f;
	fRect2D graphRect = GUI::BeginSubLayout(LAYOUT_VERTICAL, 128);
	GUI::EndLayout();
	GUI::mLayoutTheme = theme;

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
		snprintf(buf, 64, "%ums", (uint32_t)graphWindowMax);
		GUI::DrawString(font, buf, graphTextColor,graphRect.mOffset + float2(2, graphRect.mExtent.y - 10), 1, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MIN, depth, clipRect);
		snprintf(buf, 64, "%ums", (uint32_t)graphWindowMin);
		GUI::DrawString(font, buf, graphTextColor,graphRect.mOffset + float2(2, 10), 1, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MAX, depth, clipRect);
		// Horizontal graph lines
		for (uint32_t i = 1; i < 3; i++) {
			snprintf(buf, 128, "%.1fms", graphWindowMax * i / 3.f);
			GUI::DrawString(font, buf, graphTextColor, graphRect.mOffset + float2(2, graphRect.mExtent.y * (i / 3.f)), 1, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, depth, clipRect);
			GUI::Rect(fRect2D(graphRect.mOffset.x + 32, graphRect.mOffset.y + graphRect.mExtent.y * (i / 3.f) - 1, graphRect.mExtent.x - 32, 1), graphAxisColor, nullptr, 0, depth, clipRect);
		}
		
		// Graph plot line
		for (uint32_t i = 0; i < pointCount; i++) {
			points[i].x = (float)i / ((float)pointCount - 1.f);
			points[i].y = (points[i].y - graphWindowMin) / (graphWindowMax - graphWindowMin);
		}
		GUI::PolyLine(points, pointCount, graphLineColor, 1.25f, graphRect.mOffset, graphRect.mExtent, depth, clipRect);
		delete[] points;

	}

	GUI::EndLayout(); // scroll

	GUI::EndLayout(); // window
	
	return;
}