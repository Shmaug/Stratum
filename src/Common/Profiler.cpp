#include "Profiler.hpp"
#include "Window.hpp"

#include <array>
#include <imgui/imgui.h>

using namespace stm;

list<ProfilerSample> Profiler::mFrameHistory;
ProfilerSample* Profiler::mCurrentSample = nullptr;
size_t Profiler::mHistoryCount = 256;
bool Profiler::mPaused = false;
unique_ptr<ProfilerSample> Profiler::mTimelineSample = nullptr;

namespace ProfilerGui
{

static ImVec2 startOffset = ImVec2(10.f, 10.f);
static constexpr float sectionHeight = 20.f;
static constexpr float yPad = 5.f;

using frame_time_unit = chrono::duration<float, milli>;

template<typename T>
struct Range2 {
	T min, max;
};

using Range2f = Range2<float>;
using Range2t = Range2<chrono::steady_clock::time_point>;

static inline float logx(const float x, const float n)
{
    return log(n) / log(x);
}

static inline string formatFloat(const float a, const streamsize precision = 1)
{
    stringstream stream;
    stream << std::fixed << std::setprecision(precision) << a;
    return stream.str();
}

static inline unique_ptr<ProfilerSample> copyProfilerSample(const ProfilerSample& sample, ProfilerSample* parent = nullptr)
{
    unique_ptr<ProfilerSample> s = make_unique<ProfilerSample>();
    s->mColor = sample.mColor;
    s->mDuration = sample.mDuration;
    s->mLabel = sample.mLabel;
    s->mParent = parent;
    s->mStartTime = sample.mStartTime;
    
    for (const unique_ptr<ProfilerSample>& child : sample.mChildren)
    {
        unique_ptr<ProfilerSample> childCopy = copyProfilerSample(*child, s.get());
        s->mChildren.push_back(move(childCopy));
    }

    return s;
}

static void createPlotGridlines(const Range2f& ssRange, const Range2f& outRange, vector<float>& minor, vector<float>& major, vector<string>& labels)
{
    static constexpr uint32_t numMinorSubdivisions = 5;

    auto remap = [](const float n, const Range2f& fromRange, const Range2f& toRange) {
        const float norm = (n - fromRange.min) / (fromRange.max - fromRange.min);
        return norm * (toRange.max - toRange.min) + toRange.min;
    };

    const float height = outRange.max - outRange.min;
    const int32_t order = pow(10.f, floor(log10f(height)));

    if (order == 0)
    {
        return;
    }

    const static std::array<int32_t, 2> bases = { 2, 5 };
    const int32_t scale = uint32_t(height / order);

    int32_t base = bases[0];
    for(const int32_t other : span(next(begin(bases)), end(bases)))
    {
        if(abs(scale - other) < abs(scale - base))
        {
            base = other;
        }
    }

    const float majorGridStep = pow(base, floor(logx(base, scale))) * order;
    const float firstMajorLine = floor(outRange.min / majorGridStep) * majorGridStep;
    const float lastMajorLine = floor(outRange.max / majorGridStep) * majorGridStep;

    for (float curMajorLine = firstMajorLine; curMajorLine <= lastMajorLine; curMajorLine += majorGridStep)
	{
        major.push_back(remap(curMajorLine, outRange, ssRange));
        labels.push_back(formatFloat(curMajorLine));

        for(uint32_t i = 1; i < numMinorSubdivisions; i++)
        {
            const float outMinor = curMajorLine + i * majorGridStep / numMinorSubdivisions;
            const float ssMinor = remap(outMinor, outRange, ssRange);

            minor.push_back(ssMinor);
        }
    }
}

template<typename OnSampleSelectedFunc>
static void DrawPlot(float* data, size_t size, ImVec2 dim, const OnSampleSelectedFunc& onSampleSelected)
{
    assert(size > 1);

    auto ImLerp = [](const ImVec2& a, const ImVec2& b, const ImVec2& t) { return ImVec2(a.x + (b.x - a.x) * t.x, a.y + (b.y - a.y) * t.y); };

    const int res_w = std::min(size, size_t(dim.x)) - 1;
    const float t_step = 1.f / res_w;

    float v_min = 0.f;
    float v_max = -FLT_MAX;
    for (int i = 0; i < size; i++)
    {
        const float v = data[i];
        if (v != v) // Ignore NaN values
            continue;
        //v_min = std::min(v_min, v);
        v_max = std::max(v_max, v);
    }

    struct Rect2
    {
        ImVec2 Min, Max;
    };

    const ImVec2 graphPad = ImVec2(10.f, 10.f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const Rect2 frame_bb = { ImVec2(cursorPos.x + startOffset.x, cursorPos.y + startOffset.y),
        ImVec2(cursorPos.x + dim.x + startOffset.x, cursorPos.y + dim.y + startOffset.y) };
    const Rect2 inner_bb = { ImVec2(frame_bb.Min.x + graphPad.x, frame_bb.Min.y),
        ImVec2(frame_bb.Max.x, frame_bb.Max.y - graphPad.y) };
    //drawList->AddRectFilled(frame_bb.Min, frame_bb.Max, IM_COL32(0, 0, 0, 0), 0.f);

    const float inv_scale = 1.f / (v_max - v_min);

    const ImU32 colBase = ImGui::GetColorU32(ImGuiCol_PlotLines);
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    float t1 = 0.f;
    for (size_t i = 1; i < size; i++)
    {
        const float t2 = t1 + t_step;
        const float v1 = (data[i-1] - v_min) * inv_scale;
        const float v2 = (data[i] - v_min) * inv_scale;
       
        const ImVec2 p1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t1, 1.f - v1));
        const ImVec2 p2 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t2, 1.f - v2));

        drawList->AddLine(p1, p2, colBase, 1.1f);
        t1 = t2;
    }

    // tooltip / mouse vert line
    const ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.y > frame_bb.Min.y && mousePos.y < frame_bb.Max.y && mousePos.x > frame_bb.Min.x && mousePos.x < frame_bb.Max.x)
    {
        const float idxNorm = (mousePos.x - frame_bb.Min.x) / (frame_bb.Max.x - frame_bb.Min.x);
        const size_t idx = size_t(idxNorm * float(size));
        ImGui::BeginTooltip();
        ImGui::Text(std::to_string(data[idx]).c_str());
        ImGui::EndTooltip();

        const ImVec2 p1 = ImVec2(mousePos.x, inner_bb.Min.y);
        const ImVec2 p2 = ImVec2(mousePos.x, inner_bb.Max.y);
        drawList->AddLine(p1, p2, colBase, 1.1f);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            onSampleSelected(idx);
        }
    }

    static constexpr float tickHeight = 10.f;
    { // y axis
        {
            const ImVec2 p1 = ImVec2(frame_bb.Min.x, inner_bb.Min.y);
            const ImVec2 p2 = ImVec2(frame_bb.Min.x, inner_bb.Max.y);
            drawList->AddLine(p1, p2, colBase, 1.1f);
        }

        vector<float> minor,  major;
        vector<string> labels;
        createPlotGridlines(Range2f(inner_bb.Max.y, inner_bb.Min.y), Range2f(v_min, v_max), minor, major, labels);

        { // minor gridlines
            const float tickStartX = frame_bb.Min.x + (tickHeight * .5f);
            const float tickEndX = frame_bb.Min.x - (tickHeight * .5f);
            for (size_t i = 0; i < minor.size(); i++)
            {
                const float v = minor[i];
                if (v < inner_bb.Min.y)
                {
                    break;
                }

                const ImVec2 p1 = ImVec2(tickStartX, v), p2 = ImVec2(tickEndX, v);
                drawList->AddLine(p1, p2, colBase, 1.1f);
            }
        }

        { // major gridlines
            const float tickStartX = frame_bb.Min.x + (tickHeight * .9f);
            const float tickEndX = frame_bb.Min.x - (tickHeight * .9f);
            for (size_t i = 0; i < major.size(); i++)
            {
                const float v = major[i];
                const ImVec2 p1 = ImVec2(tickStartX, v), p2 = ImVec2(tickEndX, v);
                drawList->AddLine(p1, p2, colBase, 1.1f);

                const string& label = labels[i];
                drawList->AddText(p1, colText, label.c_str());
            }
        }
    }

    ImGui::NewLine();
    ImGui::Dummy(dim);
    ImGui::NewLine();
}

static void DrawTimelineSection(ImDrawList* drawList, const Range2f& range, string_view label, float timeScale, uint32_t offset)
{
    const ImVec2 sectionDim = ImVec2((range.max - range.min) * timeScale, sectionHeight);
    const ImVec2 lowerBoundOffset = ImVec2(timeScale * range.min, sectionHeight * float(offset) + yPad * float(offset) + 10.f);
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    const ImVec2 lowerBound = ImVec2(cursorPos.x + lowerBoundOffset.x + startOffset.x,
        cursorPos.y + lowerBoundOffset.y + startOffset.y);
    const ImVec2 upperBound = ImVec2(lowerBound.x + sectionDim.x, lowerBound.y + sectionDim.y);

    constexpr float darkerPct = .6f;
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec4 colFloats = ImVec4(0.7f, 0.25f, 0.96f, 1.f);
    const ImVec4 colDarkerFloats = ImVec4(colFloats.x * darkerPct, colFloats.y * darkerPct, colFloats.z * darkerPct, colFloats.w);
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.25f, 0.96f, 1.f));
    const ImU32 colDarker = ImGui::ColorConvertFloat4ToU32(colDarkerFloats);
    drawList->AddRectFilled(lowerBound, upperBound, col);
    drawList->AddRect(lowerBound, upperBound, colDarker, 1.f, 0, 1.f); // TODO: this border is drawn weird

    const ImVec4 clipRect = ImVec4(lowerBound.x, lowerBound.y, upperBound.x, upperBound.y);
    drawList->AddText(nullptr, 0.f, lowerBound, colText, label.data(), nullptr, 0.f, &clipRect);

    const ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.y > lowerBound.y && mousePos.y < upperBound.y && mousePos.x > lowerBound.x && mousePos.x < upperBound.x)
    {
        ImGui::BeginTooltip();
        ImGui::Text(label.data());
        ImGui::EndTooltip();
    }
}

static void DrawTimeline(ImDrawList* drawList, const Range2t& frameRange, const list<unique_ptr<ProfilerSample>>& times, float width, uint32_t offset, uint32_t& maxOffset)
{
    const float frameDuration = chrono::duration_cast<frame_time_unit>(frameRange.max - frameRange.min).count();
    const float timeScale = width / frameDuration;
    maxOffset = max(offset + 1, maxOffset);
    for (const unique_ptr<ProfilerSample>& inner : times)
    {
        const float sectionBegin = chrono::duration_cast<frame_time_unit>(inner->mStartTime - frameRange.min).count();
        const float sectionEnd = chrono::duration_cast<frame_time_unit>((inner->mStartTime + inner->mDuration) - frameRange.min).count();
        DrawTimelineSection(drawList, Range2f{sectionBegin, sectionEnd}, inner->mLabel, timeScale, offset);
        DrawTimeline(drawList, frameRange, inner->mChildren, width, offset + 1, maxOffset);
    }
}

static void DrawTimeline(const ProfilerSample& sample)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const Range2t frameRange = Range2t{ sample.mStartTime, sample.mStartTime + sample.mDuration };
    const Range2f frameRangeMs = Range2f{ 0.f, chrono::duration_cast<frame_time_unit>(sample.mDuration).count() };

    const ImU32 colBase = ImGui::GetColorU32(ImGuiCol_PlotLines);
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    static constexpr float lengthX = 400.f; // hardcoded for now
    const ImVec2 cursorPos = ImGui::GetCursorScreenPos(); // ImVec2(ImGui::GetCursorPosX() + startOffset.x, ImGui::GetCursorPosY() + startOffset.y);
    {
        {
            const ImVec2 p1 = cursorPos;
            const ImVec2 p2 = ImVec2(cursorPos.x + lengthX, cursorPos.y);
            drawList->AddLine(p1, p2, colBase, 1.1f);
        }

        vector<float> minor, major;
        vector<string> labels;
        createPlotGridlines(Range2f(cursorPos.x, cursorPos.x + lengthX), frameRangeMs, minor, major, labels);

        static constexpr float tickHeight = 10.f;
        { // minor gridlines
            const float tickStartY = cursorPos.y + (tickHeight * .5f);
            const float tickEndY = cursorPos.y - (tickHeight * .5f);
            for (size_t i = 0; i < minor.size(); i++)
            {
                if (minor[i] > cursorPos.x + lengthX)
                {
                    break;
                }

                const ImVec2 p1 = ImVec2(minor[i], tickStartY);
                const ImVec2 p2 = ImVec2(minor[i], tickEndY);
                drawList->AddLine(p1, p2, colBase);
            }
        }

        { // major gridlines
            const float tickStartY = cursorPos.y + (tickHeight * .5f);
            const float tickEndY = cursorPos.y - (tickHeight * .5f);
            for (size_t i = 0; i < major.size(); i++)
            {

                const ImVec2 p1 = ImVec2(major[i], tickStartY);
                const ImVec2 p2 = ImVec2(major[i], tickEndY);
                drawList->AddLine(p1, p2, colBase);
                drawList->AddText(ImVec2(p1.x, p1.y - 30.f), colText, (labels[i] + "ms").c_str());
            }
        }
    }

    uint32_t maxOffset = 0;
    DrawTimeline(drawList, frameRange, sample.mChildren, lengthX, 0, maxOffset);

    const float dummyHeight = maxOffset * (sectionHeight + yPad) + yPad * 2.f;
    ImGui::Dummy(ImVec2(lengthX, dummyHeight));
}

} // namespace ProfilerGui

void Profiler::DrawGui() {

	ImGui::Begin("profiler"); 
	
	if (mFrameHistory.size()) {
		auto getSampleDurationMillis = [](const ProfilerSample& sample) -> float { 
			return chrono::duration_cast<chrono::duration<float, milli>>(sample.mDuration).count();
		};

		auto sampleTimings =  mFrameHistory | views::transform(getSampleDurationMillis);
		vector<float> frameTimings;
		ranges::copy(ranges::begin(sampleTimings), ranges::end(sampleTimings), back_inserter(frameTimings));

        ProfilerGui::DrawPlot(frameTimings.data(), frameTimings.size(), ImVec2(400, 100), [](const size_t idx) {
            const ProfilerSample& sample = *next(begin(mFrameHistory), idx);
            mTimelineSample = ProfilerGui::copyProfilerSample(sample); 
        });

        const ProfilerSample& timelineSample = mTimelineSample ? *mTimelineSample : *next(begin(mFrameHistory));
        ProfilerGui::DrawTimeline(timelineSample);

        if (mPaused ? ImGui::Button("resume") : ImGui::Button("pause"))
        {
            mPaused = !mPaused;
        }
	}

	ImGui::End();
}