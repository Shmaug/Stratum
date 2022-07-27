#include "Profiler.hpp"

#include <imgui/imgui.h>

using namespace stm;

shared_ptr<Profiler::sample_t> Profiler::mCurrentSample;
vector<pair<chrono::steady_clock::time_point, vector<pair<string,chrono::nanoseconds>>>> Profiler::mTimestamps;
vector<shared_ptr<Profiler::sample_t>> Profiler::mSampleHistory;
uint32_t Profiler::mSampleHistoryCount = 0;
optional<chrono::high_resolution_clock::time_point> Profiler::mFrameStart = nullopt;
deque<float> Profiler::mFrameTimes;
uint32_t Profiler::mFrameTimeCount = 32;

inline optional<pair<ImVec2,ImVec2>> draw_sample_timeline(const Profiler::sample_t& s, const float t0, const float t1, const float x_min, const float x_max, const float y, const float height) {
	const ImVec2 p_min = ImVec2(x_min + t0*(x_max - x_min), y);
	const ImVec2 p_max = ImVec2(x_min + t1*(x_max - x_min), y + height);
	if (p_max.x < x_min || p_max.x > x_max) return {};

	const ImVec2 mousePos = ImGui::GetMousePos();
	bool hovered = (mousePos.y > p_min.y && mousePos.y < p_max.y && mousePos.x > p_min.x && mousePos.x < p_max.x);
	if (hovered) {
		ImGui::BeginTooltip();
		const string label = s.mLabel + " (" + to_string(chrono::duration_cast<chrono::duration<float, milli>>(s.mDuration).count()) + "ms)";
		ImGui::Text(label.c_str());
		ImGui::EndTooltip();
	}

	ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button), 4);

	const ImVec4 clipRect = ImVec4(p_min.x, p_min.y, p_max.x, p_max.y);
	ImGui::GetWindowDrawList()->AddText(nullptr, 0, p_min, ImGui::GetColorU32(ImGuiCol_Text), s.mLabel.c_str(), nullptr, 0, &clipRect);
	return make_pair(p_min, p_max);
}

void Profiler::sample_timeline_gui() {
	chrono::high_resolution_clock::time_point t_min = mSampleHistory[0]->mStartTime;
	chrono::high_resolution_clock::time_point t_max = t_min;
	for (const auto& f : mSampleHistory) {
		if (f->mStartTime < t_min) t_min = f->mStartTime;
		if (auto t = f->mStartTime + f->mDuration; t > t_max) t_max = t;
	}

	const float inv_dt = 1/chrono::duration_cast<chrono::duration<float, milli>>(t_max - t_min).count();

	const ImVec2 w_min = ImVec2(ImGui::GetWindowContentRegionMin().x + ImGui::GetWindowPos().x, ImGui::GetWindowContentRegionMin().y + ImGui::GetWindowPos().y);
	const float x_max = w_min.x + ImGui::GetWindowContentRegionWidth();

	float height = 28;
	float pad = 4;

	// timestamps
	{
		chrono::nanoseconds gpu_t_min = mTimestamps[0].second[0].second;
		chrono::nanoseconds gpu_t_max = gpu_t_min;
		for (const auto&[ft0,f] : mTimestamps) {
			cout << "T - " << chrono::duration_cast<chrono::duration<float, milli>>(chrono::high_resolution_clock::now() - ft0).count() << "ms" << endl;
			for (uint32_t i = 0; i < f.size(); i++) {
				const auto&[l,t] = f[i];
				cout << "\t" << l << ":\t" << t.count() << "ns";
				if (i > 0)
					cout << "\t(" << chrono::duration_cast<chrono::duration<float, milli>>(t - f[i-1].second).count() << "ms)";
				cout << endl;
				if (t < gpu_t_min) gpu_t_min = t;
				if (t > gpu_t_max) gpu_t_max = t;
			}
		}
		const double inv_gpu_dt = 1.0 / (gpu_t_max.count() - gpu_t_min.count());
		for (const auto&[ft0,f] : mTimestamps)
			for (uint32_t i = 1; i < f.size(); i++) {
				const auto&[label0,t0] = f[i-1];
				const auto&[label1,t1] = f[i];
				Profiler::sample_t tmp;
				tmp.mDuration = t1 - t0;
				tmp.mColor = float4::Ones();
				tmp.mLabel = label0;
				draw_sample_timeline(tmp, t0.count()*inv_gpu_dt, t1.count()*inv_gpu_dt, w_min.x, x_max, w_min.y, height);
			}
	}

	// profiler sample history
	{
		stack<pair<shared_ptr<Profiler::sample_t>, uint32_t>> todo;
		for (const auto& f : mSampleHistory) todo.push(make_pair(f, 1));
		while (!todo.empty()) {
			auto[s,l] = todo.top();
			todo.pop();

			const float t0 = chrono::duration_cast<chrono::duration<float, milli>>(s->mStartTime - t_min).count() * inv_dt;
			const float t1 = chrono::duration_cast<chrono::duration<float, milli>>(s->mStartTime + s->mDuration - t_min).count() * inv_dt;
			auto r = draw_sample_timeline(*s, t0, t1, w_min.x, x_max, w_min.y + l*(height + pad), height);
			if (!r) continue;

			const auto[p_min,p_max] = *r;

			for (const auto& c : s->mChildren)
				todo.push(make_pair(c, l+1));
		}
	}
}

void Profiler::frame_times_gui() {
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
	ImGui::SliderInt("Frame Time Count", reinterpret_cast<int*>(&mFrameTimeCount), 2, 256);
	if (frame_times.size() > 1) ImGui::PlotLines("Frame Times", frame_times.data(), (uint32_t)frame_times.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 64));
}