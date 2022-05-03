#include "Profiler.hpp"

#include <imgui/imgui.h>

using namespace stm;

shared_ptr<Profiler::sample_t> Profiler::mCurrentSample;
deque<float> Profiler::mFrameTimes;
vector<shared_ptr<Profiler::sample_t>> Profiler::mFrameHistory;
uint32_t Profiler::mFrameTimeCount = 256;
uint32_t Profiler::mFrameHistoryCount = 0;

inline optional<pair<ImVec2,ImVec2>> draw_sample_timeline(const Profiler::sample_t& s, const chrono::high_resolution_clock::time_point& t_min, const chrono::high_resolution_clock::time_point& t_max, const float x_min, const float x_max, const float y, const float height) {
	const float dt = chrono::duration_cast<chrono::duration<float, milli>>(t_max - t_min).count();
	const float t0 = chrono::duration_cast<chrono::duration<float, milli>>(s.mStartTime - t_min).count() / dt;
	const float t1 = chrono::duration_cast<chrono::duration<float, milli>>(s.mStartTime + s.mDuration - t_min).count() / dt;

	const ImVec2 p_min = ImVec2(x_min + t0*(x_max - x_min), y);
	const ImVec2 p_max = ImVec2(x_min + t1*(x_max - x_min), y + height);
	if (p_max.x < x_min || p_max.x > x_max) return {};

	const ImVec2 mousePos = ImGui::GetMousePos();
	bool hovered = (mousePos.y > p_min.y && mousePos.y < p_max.y && mousePos.x > p_min.x && mousePos.x < p_max.x);
	if (hovered) {
		ImGui::BeginTooltip();
		ImGui::Text(s.mLabel.c_str());
		ImGui::EndTooltip();
	}

	ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button), 4);

	const ImVec4 clipRect = ImVec4(p_min.x, p_min.y, p_max.x, p_max.y);
	ImGui::GetWindowDrawList()->AddText(nullptr, 0, p_min, ImGui::GetColorU32(ImGuiCol_Text), s.mLabel.c_str(), nullptr, 0, &clipRect);
	return make_pair(p_min, p_max);
}

void Profiler::timeline_gui() {
	chrono::high_resolution_clock::time_point t_min = mFrameHistory[0]->mStartTime;
	chrono::high_resolution_clock::time_point t_max = mFrameHistory[0]->mStartTime;
	for (const auto& f : mFrameHistory) {
		if (f->mStartTime < t_min) t_min = f->mStartTime;
		if (auto t = f->mStartTime + f->mDuration; t > t_max) t_max = t;
	}

	const ImVec2 w_min = ImVec2(ImGui::GetWindowContentRegionMin().x + ImGui::GetWindowPos().x, ImGui::GetWindowContentRegionMin().y + ImGui::GetWindowPos().y);
	const float x_max = w_min.x + ImGui::GetWindowContentRegionWidth();

	float height = 28;
	float pad = 4;

	stack<pair<shared_ptr<Profiler::sample_t>, uint32_t>> todo;
	for (const auto& f : mFrameHistory) todo.push(make_pair(f, 0));
	while (!todo.empty()) {
		auto[s,l] = todo.top();
		todo.pop();

		auto r = draw_sample_timeline(*s, t_min, t_max, w_min.x, x_max, w_min.y + l*(height + pad), height);
		if (!r) continue;

		const auto[p_min,p_max] = *r;

		for (const auto& c : s->mChildren)
			todo.push(make_pair(c, l+1));
	}
}

void Profiler::timings_gui() {
	float fps_timer = 0;
	uint32_t fps_counter = 0;
	vector<float> frame_times(mFrameTimes.size());
	for (uint32_t i = 0; i < mFrameTimes.size(); i++) {
		if (fps_timer < 1000.f) {
			fps_timer += mFrameTimes[i];
			fps_counter++;
		}
		frame_times[i] = mFrameTimes[i];
	}

	ImGui::Text("%.1f fps (%.1f ms)", fps_counter/(fps_timer/1000), fps_timer/fps_counter);
	ImGui::SliderInt("Length", reinterpret_cast<int*>(&mFrameTimeCount), 2, 2048);
	if (frame_times.size() > 1) ImGui::PlotLines("Frame Times", frame_times.data(), (uint32_t)frame_times.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 64));
}