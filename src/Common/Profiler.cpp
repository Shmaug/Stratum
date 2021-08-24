#include "Profiler.hpp"
#include <Core/Window.hpp>

#include <imgui.h>

using namespace stm;

list<shared_ptr<Profiler::sample_t>> Profiler::mFrameHistory;
shared_ptr<Profiler::sample_t> Profiler::mCurrentSample;
size_t Profiler::mFrameHistoryCount = 256;

template<typename T>
struct Range2 { T min, max; };

using Range2f = Range2<float>;
using Range2t = Range2<chrono::steady_clock::time_point>;
using frame_time_unit = chrono::duration<float, milli>;

ImVec2 startOffset = ImVec2(10.f, 10.f);
constexpr float sectionHeight = 20.f;
constexpr float yPad = 5.f;
bool mPaused = false;
shared_ptr<Profiler::sample_t> mTimelineSample;

inline float logx(const float x, const float n) {
  return log(n) / log(x);
}
inline string formatFloat(const float a, const streamsize precision = 1) {
  stringstream stream;
  stream << std::fixed << std::setprecision(precision) << a;
  return stream.str();
}

inline void createPlotGridlines(const Range2f& ssRange, const Range2f& outRange, vector<float>& minor, vector<float>& major, vector<string>& labels) {
  static constexpr uint32_t numMinorSubdivisions = 5;

  auto remap = [](const float n, const Range2f& fromRange, const Range2f& toRange) {
    const float norm = (n - fromRange.min) / (fromRange.max - fromRange.min);
    return norm * (toRange.max - toRange.min) + toRange.min;
  };

  const float height = outRange.max - outRange.min;
  const int32_t order = (int32_t)pow(10.f, floor(log10f(height)));

  if (order == 0) return;

  const static std::array<int32_t, 2> bases = { 2, 5 };
  const int32_t scale = uint32_t(height / order);

  int32_t base = bases[0];
  for (const int32_t other : span(next(begin(bases)), end(bases)))
    if (abs(scale - other) < abs(scale - base))
      base = other;

  const float majorGridStep = pow((float)base, floor(logx((float)base, (float)scale))) * order;
  const float firstMajorLine = floor(outRange.min / majorGridStep) * majorGridStep;
  const float lastMajorLine = floor(outRange.max / majorGridStep) * majorGridStep;

  for (float curMajorLine = firstMajorLine; curMajorLine <= lastMajorLine; curMajorLine += majorGridStep) {
    major.push_back(remap(curMajorLine, outRange, ssRange));
    labels.push_back(formatFloat(curMajorLine));

    for (uint32_t i = 1; i < numMinorSubdivisions; i++) {
      const float outMinor = curMajorLine + i * majorGridStep / numMinorSubdivisions;
      const float ssMinor = remap(outMinor, outRange, ssRange);

      minor.push_back(ssMinor);
    }
  }
}

template<ranges::random_access_range R, typename OnSampleSelectedFunc>
inline void DrawPlot(const R& data, const float width, const float height, const OnSampleSelectedFunc& onSampleSelected) {
  assert(ranges::size(data) > 1);

  auto ImLerp = [](const ImVec2& a, const ImVec2& b, const ImVec2& t) { return ImVec2(a.x + (b.x - a.x) * t.x, a.y + (b.y - a.y) * t.y); };

  const size_t res_w = min(ranges::size(data), size_t(width)) - 1;
  const float t_step = 1.f / res_w;

  float v_min = 0.f;
  float v_max = -FLT_MAX;
  for (int i = 0; i < ranges::size(data); i++) {
    const float v = data[i];
    if (v != v) continue; // Ignore NaN values
    //v_min = std::min(v_min, v);
    v_max = std::max(v_max, v);
  }

  struct Rect2 {
    ImVec2 Min, Max;
  };

  const ImVec2 graphPad(10, 10);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
  const Rect2 frame_bb = { ImVec2(cursorPos.x + startOffset.x, cursorPos.y + startOffset.y),
      ImVec2(cursorPos.x + width + startOffset.x, cursorPos.y + height + startOffset.y) };
  const Rect2 inner_bb = { ImVec2(frame_bb.Min.x + graphPad.x, frame_bb.Min.y),
      ImVec2(frame_bb.Max.x, frame_bb.Max.y - graphPad.y) };
  //drawList->AddRectFilled(frame_bb.Min, frame_bb.Max, IM_COL32(0, 0, 0, 0), 0.f);

  const float inv_scale = 1 / (v_max - v_min);

  const ImU32 colBase = ImGui::GetColorU32(ImGuiCol_PlotLines);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
  float t1 = 0;
  for (size_t i = 1; i < ranges::size(data); i++) {
    const float v1 = (data[i - 1] - v_min) * inv_scale;
    const float v2 = (data[i] - v_min) * inv_scale;

    const ImVec2 p1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t1, 1.f - v1));
    t1 += t_step;
    const ImVec2 p2 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t1, 1.f - v2));
    drawList->AddLine(p1, p2, colBase, 1.1f);
  }

  // tooltip / mouse vert line
  const ImVec2 mousePos = ImGui::GetMousePos();
  if (mousePos.y > frame_bb.Min.y && mousePos.y < frame_bb.Max.y && mousePos.x > frame_bb.Min.x && mousePos.x < frame_bb.Max.x) {
    const float idxNorm = (mousePos.x - frame_bb.Min.x) / (frame_bb.Max.x - frame_bb.Min.x);
    const size_t idx = size_t(idxNorm * float(ranges::size(data)));
    ImGui::BeginTooltip();
    ImGui::Text(std::to_string(data[idx]).c_str());
    ImGui::EndTooltip();

    const ImVec2 p1 = ImVec2(mousePos.x, inner_bb.Min.y);
    const ImVec2 p2 = ImVec2(mousePos.x, inner_bb.Max.y);
    drawList->AddLine(p1, p2, colBase, 1.1f);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      onSampleSelected(idx);
  }

  constexpr float tickHeight = 10;
  { // y axis
    drawList->AddLine(ImVec2(frame_bb.Min.x, inner_bb.Min.y), ImVec2(frame_bb.Min.x, inner_bb.Max.y), colBase, 1.1f);

    vector<float> minor, major;
    vector<string> labels;
    createPlotGridlines(Range2f(inner_bb.Max.y, inner_bb.Min.y), Range2f(v_min, v_max), minor, major, labels);

    { // minor gridlines
      const float tickStartX = frame_bb.Min.x + tickHeight/2;
      const float tickEndX   = frame_bb.Min.x - tickHeight/2;
      for (size_t i = 0; i < minor.size(); i++) {
        if (minor[i] < inner_bb.Min.y) break;
        drawList->AddLine(ImVec2(tickStartX, minor[i]), ImVec2(tickEndX, minor[i]), colBase, 1.1f);
      }
    }

    { // major gridlines
      const float tickStartX = frame_bb.Min.x + tickHeight*.9f;
      const float tickEndX   = frame_bb.Min.x - tickHeight*.9f;
      for (size_t i = 0; i < major.size(); i++) {
        const ImVec2 p1 = ImVec2(tickStartX, major[i]);
        drawList->AddLine(p1, ImVec2(tickEndX, major[i]), colBase, 1.1f);
        drawList->AddText(p1, colText, labels[i].c_str());
      }
    }
  }
  ImGui::NewLine();
  ImGui::Dummy(ImVec2(width, height));
  ImGui::NewLine();
}

inline void DrawTimelineSection(ImDrawList* drawList, const Range2f& range, string_view label, float timeScale, uint32_t offset) {
  const ImVec2 sectionDim = ImVec2((range.max - range.min) * timeScale, sectionHeight);
  const ImVec2 lowerBoundOffset = ImVec2(timeScale * range.min, sectionHeight * float(offset) + yPad * float(offset) + 10);
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
  drawList->AddRect(lowerBound, upperBound, colDarker, 1, 0, 1); // TODO: this border is drawn weird

  const ImVec4 clipRect = ImVec4(lowerBound.x, lowerBound.y, upperBound.x, upperBound.y);
  drawList->AddText(nullptr, 0, lowerBound, colText, label.data(), nullptr, 0, &clipRect);

  const ImVec2 mousePos = ImGui::GetMousePos();
  if (mousePos.y > lowerBound.y && mousePos.y < upperBound.y && mousePos.x > lowerBound.x && mousePos.x < upperBound.x) {
    ImGui::BeginTooltip();
    ImGui::Text(label.data());
    ImGui::EndTooltip();
  }
}

inline void DrawTimeline(ImDrawList* drawList, const float width, const Range2t& frameRange, const list<shared_ptr<Profiler::sample_t>>& times, uint32_t offset, uint32_t& maxOffset) {
  const float frameDuration = chrono::duration_cast<frame_time_unit>(frameRange.max - frameRange.min).count();
  const float timeScale = width / frameDuration;
  maxOffset = max(offset + 1, maxOffset);
  for (const auto& inner : times) {
    const float sectionBegin = chrono::duration_cast<frame_time_unit>(inner->mStartTime - frameRange.min).count();
    const float sectionEnd   = chrono::duration_cast<frame_time_unit>((inner->mStartTime + inner->mDuration) - frameRange.min).count();
    DrawTimelineSection(drawList, Range2f{ sectionBegin, sectionEnd }, inner->mLabel, timeScale, offset);
    DrawTimeline(drawList, width, frameRange, inner->mChildren, offset + 1, maxOffset);
  }
}

inline void DrawTimeline(const Profiler::sample_t& sample, const float width, const float tickHeight = 10.f) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  const Range2t frameRange(sample.mStartTime, sample.mStartTime + sample.mDuration);
  const Range2f frameRangeMs(0, chrono::duration_cast<frame_time_unit>(sample.mDuration).count());

  const ImU32 colBase = ImGui::GetColorU32(ImGuiCol_PlotLines);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
  const ImVec2 cursorPos = ImGui::GetCursorScreenPos(); // ImVec2(ImGui::GetCursorPosX() + startOffset.x, ImGui::GetCursorPosY() + startOffset.y);

  drawList->AddLine(
    cursorPos, 
    ImVec2(cursorPos.x + width, cursorPos.y),
    colBase, 1.1f);

  vector<float> minor, major;
  vector<string> labels;
  createPlotGridlines(Range2f(cursorPos.x, cursorPos.x + width), frameRangeMs, minor, major, labels);

  const float tickStartY = cursorPos.y + tickHeight/2;
  const float tickEndY = cursorPos.y - tickHeight/2;
  
  // minor gridlines
  for (float m : minor) {
    if (m > cursorPos.x + width)
      break;
    drawList->AddLine(
      ImVec2(m, tickStartY),
      ImVec2(m, tickEndY),
      colBase);
  }

  // major gridlines
  for (size_t i = 0; i < major.size(); i++) {
    drawList->AddLine(
      ImVec2(major[i], tickStartY),
      ImVec2(major[i], tickEndY),
      colBase);
    drawList->AddText(
      ImVec2(major[i], tickStartY - 30),
      colText, (labels[i] + "ms").c_str());
  }

  uint32_t maxOffset = 0;
  DrawTimeline(drawList, width, frameRange, sample.mChildren, 0, maxOffset);

  const float dummyHeight = maxOffset * (sectionHeight + yPad) + yPad*2;
  ImGui::Dummy(ImVec2(width, dummyHeight));
}

void Profiler::on_gui() {
  if (!mFrameHistory.empty() && ImGui::Begin("Profiler")) {
    vector<float> frameTimings(mFrameHistory.size());
    ranges::transform(next(mFrameHistory.begin()), mFrameHistory.end(), frameTimings.begin(), [](const auto& s) {
      return chrono::duration_cast<chrono::duration<float, milli>>(s->mDuration).count();
    });

    const float graphScale = 2;
    
    float width = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    mFrameHistoryCount = size_t(width / graphScale);
    width = min(width, graphScale*frameTimings.size());
    
    const float height = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;

    DrawPlot(frameTimings, width, height/4, [&](const size_t idx) {
      mTimelineSample = *next(mFrameHistory.begin(), idx+1);
    });
    DrawTimeline(mTimelineSample ? *mTimelineSample : **mFrameHistory.begin(), width);
  }
  ImGui::End();
}