#include "Profiler.hpp"

#include <imgui/imgui.h>

using namespace stm;

shared_ptr<Profiler::sample_t> Profiler::mCurrentSample;
deque<float> Profiler::mFrameTimes;
vector<shared_ptr<Profiler::sample_t>> Profiler::mFrameHistory;
uint32_t Profiler::mFrameTimeCount = 256;
uint32_t Profiler::mFrameHistoryCount = 0;

inline void draw_sample_timeline(const Profiler::sample_t& s, const uint32_t level, const chrono::high_resolution_clock::time_point& t_min, const chrono::high_resolution_clock::time_point& t_max, const float width, const float height, const float pad) {
	const float dt = chrono::duration_cast<chrono::duration<float, milli>>(t_max - t_min).count();
	const float t0 = chrono::duration_cast<chrono::duration<float, milli>>(s.mStartTime - t_min).count() / dt;
	const float t1 = chrono::duration_cast<chrono::duration<float, milli>>(s.mStartTime + s.mDuration - t_min).count() / dt;

	const ImVec2 p_min = ImVec2(t0*width, (height+pad)*level);
	const ImVec2 p_max = ImVec2(t1*width, (height+pad)*level + height);
	ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(ImVec4(s.mColor[0], s.mColor[1], s.mColor[2], s.mColor[3])));
	cout << s.mLabel << " | " << p_min.x << ", " << p_min.y << " | " << p_max.x << ", " << p_min.y << endl;

	const ImVec4 clipRect = ImVec4(p_min.x, p_min.y, p_max.x, p_max.y);
	ImGui::GetWindowDrawList()->AddText(nullptr, 0, p_min, ImGui::GetColorU32(ImGuiCol_Text), s.mLabel.c_str(), nullptr, 0, &clipRect);

	const ImVec2 mousePos = ImGui::GetMousePos();
	if (mousePos.y > p_min.y && mousePos.y < p_max.y && mousePos.x > p_min.x && mousePos.x < p_max.x) {
		ImGui::BeginTooltip();
		ImGui::Text(s.mLabel.c_str());
		ImGui::EndTooltip();
	}
}

void Profiler::on_gui() {
	float fps_timer = 0;
	uint32_t fps_counter = 0;
	vector<float> frame_times(mFrameTimes.size());
	for (uint32_t i = 0; i < mFrameTimes.size(); i++) {
		if (mFrameTimes[i] != mFrameTimes[i]) continue;
		if (fps_timer < 1000.f) {
			fps_timer += mFrameTimes[i];
			fps_counter++;
		}
		frame_times[i] = mFrameTimes[i];
	}

	ImGui::Text("%.1f fps (%.1f ms)", fps_counter/(fps_timer/1000), fps_timer/fps_counter);

	ImGui::SliderInt("Count", reinterpret_cast<int*>(&mFrameTimeCount), 0, 1024);
	ImGui::SameLine();
	if (ImGui::Button("Timeline")) {
		if (mFrameHistory.empty())
			mFrameHistoryCount = 6;
		else
			mFrameHistory.clear();
	}
	if (frame_times.size() > 1)
		ImGui::PlotLines("Frame Times", frame_times.data(), (uint32_t)frame_times.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 64));
	if (mFrameHistory.size()) {
		if (ImGui::Begin("Timeline")) {
			static float width = 500;
			ImGui::DragFloat("width", &width);

			chrono::high_resolution_clock::time_point t_min = mFrameHistory[0]->mStartTime;
			chrono::high_resolution_clock::time_point t_max = mFrameHistory[0]->mStartTime;
			for (const auto& f : mFrameHistory) {
				if (f->mStartTime < t_min) t_min = f->mStartTime;
				if (auto t = f->mStartTime + f->mDuration; t > t_max) t_max = t;
			}

			float maxh = 0;
			const float height = 20;
			const float pad = 4;
			stack<pair<shared_ptr<sample_t>, uint32_t>> todo;
			for (const auto& f : mFrameHistory) todo.push(make_pair(f, 0));
			while (!todo.empty()) {
				auto[s,l] = todo.top();
				todo.pop();
				draw_sample_timeline(*s, l, t_min, t_max, width, height, pad);
				maxh = max(maxh, (l+1)*(height+pad));
				for (const auto& c : s->mChildren)
					todo.push(make_pair(c, l+1));
			}
		}
		ImGui::End();
	}
}