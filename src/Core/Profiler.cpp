#include "Profiler.hpp"
#include "Window.hpp"

using namespace stm;

list<ProfilerSample> Profiler::mFrameHistory;
ProfilerSample* Profiler::mCurrentSample = nullptr;
size_t Profiler::mHistoryCount = 256;

/*
void Profiler::DrawGui(GuiContext& gui, uint32_t framerate) {
	Device& device = gui.Scene().mInstance.device();
	auto font = device.FindOrLoadAsset<Font>("Assets/Fonts/OpenSans/OpenSans-Regular.ttf");

	float toolbarHeight = 24;

	Vector2f s((float)gui.Scene().mInstance.window().swapchain_extent().width, (float)gui.Scene().mInstance.window().swapchain_extent().height);
	AlignedBox2f windowRect(Vector2f::Zero(), Vector2f(s.x(), toolbarHeight + mGraphHeight));

	gui.BeginScreenLayout(GuiContext::LayoutAxis::eVertical, windowRect);

	#pragma region toolbar
	gui.BeginSubLayout(GuiContext::LayoutAxis::eHorizontal, style->mControlSize + style->mControlPadding*2);
	style->mControlSize = 140;
	if (gui.LayoutTextButton(mEnabled ? "PAUSE PROFILER" : "RESUME PROFILER")) mEnabled = !mEnabled;
	gui.LayoutLabel(to_string(framerate) + " FPS", TextAnchor::eMax);
	gui.EndLayout();
	#pragma endregion

	char buf[256];

	gui.BeginScrollSubLayout(windowRect.mSize.y - toolbarHeight, 256);

	AlignedBox2f clipRect = gui.LayoutClipRect();
	float depth = gui.LayoutDepth() - 0.001f;

	style->mBackgroundColor.xyz *= 0.8f;
	Rect2D graphRect = gui.BeginSubLayout(GuiContext::LayoutAxis::eVertical, 128);
	gui.EndLayout();
	style = style;

	// Generate graph points and vertical selection lines
	if (mFrames.size()) {
		Vector3f* points = new Vector3f[mFrames.size()];
		memset(points, 0, sizeof(Vector3f) * mFrames.size());

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
		gui.DrawString(graphRect.mOffset + Vector2f(2, graphRect.mSize.y - 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		snprintf(buf, 256, "%ums", (uint32_t)graphWindowMin);
		gui.DrawString(graphRect.mOffset + Vector2f(2, 10), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
		// Horizontal graph lines
		for (uint32_t i = 1; i < 3; i++) {
			snprintf(buf, 256, "%.1fms", graphWindowMax * i / 3.f);
			gui.DrawString(graphRect.mOffset + Vector2f(2, graphRect.mSize.y * (i / 3.f)), depth, font, 14, buf, graphTextColor, TextAnchor::eMin, clipRect);
			gui.Rect(AlignedBox2f(graphRect.mOffset.x + 32, graphRect.mOffset.y + graphRect.mSize.y * (i / 3.f) - 1, graphRect.mSize.x - 32, 1), depth, graphAxisColor, nullptr, 0, clipRect);
		}
		
		// Graph plot line
		for (uint32_t i = 0; i < pointCount; i++) {
			points[i].x = (float)i / ((float)pointCount - 1.f);
			points[i].y = (points[i].y - graphWindowMin) / (graphWindowMax - graphWindowMin);
		}
		gui.PolyLine(points, pointCount, graphLineColor, 1.25f, Vector3f(graphRect.mOffset, 0), Vector3f(graphRect.mSize, 1), clipRect);
		delete[] points;
	}

	gui.EndLayout(); // scroll
	
	gui.EndLayout(); // window
}
*/