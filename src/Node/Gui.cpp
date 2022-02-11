#include "Gui.hpp"
#include "Application.hpp"
#include "Scene.hpp"

#include <imgui_internal.h>
#include <stb_image_write.h>

#include <Core/Window.hpp>

using namespace stm;
using namespace stm::hlsl;

#ifdef WIN32
string GetSystemFontFile(const string &faceName) {
  // Open Windows font registry key
  HKEY hKey;
  LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) return "";

  DWORD maxValueNameSize, maxValueDataSize;
  result = RegQueryInfoKeyA(hKey, 0, 0, 0, 0, 0, 0, 0, &maxValueNameSize, &maxValueDataSize, 0, 0);
  if (result != ERROR_SUCCESS) return "";

  DWORD valueIndex = 0;
  string valueName;
	valueName.resize(maxValueNameSize);
  vector<BYTE> valueData(maxValueDataSize);
  string fontFile;

  // Look for a matching font name
  do {
    fontFile.clear();
    DWORD valueDataSize = maxValueDataSize;
    DWORD valueNameSize = maxValueNameSize;
		DWORD valueType;
    result = RegEnumValueA(hKey, valueIndex, valueName.data(), &valueNameSize, 0, &valueType, valueData.data(), &valueDataSize);

    valueIndex++;

    if (result != ERROR_SUCCESS || valueType != REG_SZ) continue;

    // Found a match
    if (faceName == valueName) {
      fontFile.assign((LPSTR)valueData.data(), valueDataSize);
      break;
    }
  }
  while (result != ERROR_NO_MORE_ITEMS);

  RegCloseKey(hKey);

  if (fontFile.empty()) return "";

	return "C:\\Windows\\Fonts\\" + fontFile;
}
#endif

#pragma region inspector gui
#include "RayTraceScene.hpp"
inline void inspector_gui_fn(Application* app) {
	if (ImGui::Button("Reload Shaders")) {
	  app->window().mInstance.device()->waitIdle();
		//app->window().mInstance.device().flush();
		app->load_shaders();
		app->node().for_each_descendant<Gui>([](const auto& v) { v->create_pipelines();	});
		app->node().for_each_descendant<RayTraceScene>([](const auto& v) { v->create_pipelines();	});
	}
}
inline void inspector_gui_fn(Instance* instance) {
  ImGui::Text("Vulkan %u.%u.%u", 
    VK_API_VERSION_MAJOR(instance->vulkan_version()),
    VK_API_VERSION_MINOR(instance->vulkan_version()),
    VK_API_VERSION_PATCH(instance->vulkan_version()) );

  VmaStats stats;
  vmaCalculateStats(instance->device().allocator(), &stats);
  auto used = format_bytes(stats.total.usedBytes);
  auto unused = format_bytes(stats.total.unusedBytes);
  ImGui::LabelText("Used memory", "%zu %s", used.first, used.second);
  ImGui::LabelText("Unused memory", "%zu %s", unused.first, unused.second);
  ImGui::LabelText("Device allocations", "%u", stats.total.blockCount);
  ImGui::LabelText("Descriptor Sets", "%u", instance->device().descriptor_set_count());

  ImGui::LabelText("Window resolution", "%ux%u", instance->window().swapchain_extent().width, instance->window().swapchain_extent().height);
  ImGui::LabelText("Render target format", to_string(instance->window().back_buffer().image()->format()).c_str());
  ImGui::LabelText("Surface format", to_string(instance->window().surface_format().format).c_str());
  ImGui::LabelText("Surface color space", to_string(instance->window().surface_format().colorSpace).c_str());
  
  vk::PresentModeKHR m = instance->window().present_mode();
  if (ImGui::BeginCombo("Window present mode", to_string(m).c_str())) {
		auto items = instance->device().physical().getSurfacePresentModesKHR(instance->window().surface());
    for (uint32_t i = 0; i < ranges::size(items); i++)
      if (ImGui::Selectable(to_string(items[i]).c_str(), m == items[i]))
        instance->window().preferred_present_mode(items[i]);
    ImGui::EndCombo();
	}

	int64_t timeout = instance->window().acquire_image_timeout().count();
	if (ImGui::InputScalar("Swapchain image timeout (ns)", ImGuiDataType_U64, &timeout))
		instance->window().acquire_image_timeout(chrono::nanoseconds(timeout));
}
inline void inspector_gui_fn(ShaderDatabase* shader) {
  for (const auto&[name, spv] : *shader) {
    ImGui::LabelText(name.c_str(), "%s | %s", spv->entry_point().c_str(), to_string(spv->stage()).c_str());
  }
}
inline void inspector_gui_fn(GraphicsPipelineState* pipeline) {
  ImGui::Text("%llu pipelines", pipeline->pipelines().size());
  ImGui::Text("%llu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(ComputePipelineState* pipeline) {
  ImGui::Text("%llu pipelines", pipeline->pipelines().size());
  ImGui::Text("%llu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(Mesh* mesh) {
	ImGui::LabelText("Topology", to_string(mesh->topology()).c_str());
	ImGui::LabelText("Index Type", to_string(mesh->index_type()).c_str());
  if (mesh->vertices()) {
		for (const auto&[type,verts] : *mesh->vertices())
			for (uint32_t i = 0; i < verts.size(); i++)
				if (verts[i].second && ImGui::CollapsingHeader((to_string(type) + "_" + to_string(i)).c_str())) {
					ImGui::LabelText("Format", to_string(verts[i].first.mFormat).c_str());
					ImGui::LabelText("Stride", to_string(verts[i].first.mStride).c_str());
					ImGui::LabelText("Offset", to_string(verts[i].first.mOffset).c_str());
					ImGui::LabelText("Input Rate", to_string(verts[i].first.mInputRate).c_str());
				}
  }
}

inline void node_graph_gui_fn(Node& n, Node*& selected) {
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_OpenOnArrow;
  if (&n == selected) flags |= ImGuiTreeNodeFlags_Selected;
  if (n.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  //ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx(n.name().c_str(), flags)) {
    if (ImGui::IsItemClicked())
      selected = &n;
    for (Node& c : n.children())
      node_graph_gui_fn(c, selected);
    ImGui::TreePop();
  }
}
#pragma endregion

#pragma region profiler

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

template<ranges::random_access_range R>
inline void DrawPlot(const R& data, const float width, const float height, const auto& onSampleSelected) {
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
  if (mFrameHistory.size() < 2) return;
  if (ImGui::BeginChild("Profiler", ImVec2(0, 100))) {
    vector<float> frameTimings(mFrameHistory.size());
    ranges::transform(next(mFrameHistory.begin()), mFrameHistory.end(), frameTimings.begin(), [](const auto& s) {
      return chrono::duration_cast<chrono::duration<float, milli>>(s->mDuration).count();
    });

    float timeAccum = 0; 
    uint32_t frameCount = 0;
    for (frameCount = 0; frameCount < frameTimings.size(); frameCount++) {
      timeAccum += frameTimings[frameCount];
      if (timeAccum > 2000.f) break;
    }
    ImGui::Text("%.1f fps", frameCount/(timeAccum/1000));

    const float graphScale = 2;
    
    float width = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    mFrameHistoryCount = size_t(width / graphScale);
    
    const float height = ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;

    DrawPlot(frameTimings, min(width, graphScale*frameTimings.size()), height, [&](const size_t idx) { mTimelineSample = *next(mFrameHistory.begin(), idx+1); });
    DrawTimeline(mTimelineSample ? *mTimelineSample : **mFrameHistory.begin(), width);

  }
	ImGui::EndChild();
}
#pragma endregion

Gui::Gui(Node& node) : mNode(node) {
	mContext = ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	
	io.KeyMap[ImGuiKey_Tab] = eKeyTab;
	io.KeyMap[ImGuiKey_LeftArrow] = eKeyLeft;
	io.KeyMap[ImGuiKey_RightArrow] = eKeyRight;
	io.KeyMap[ImGuiKey_UpArrow] = eKeyUp;
	io.KeyMap[ImGuiKey_DownArrow] = eKeyDown;
	io.KeyMap[ImGuiKey_PageUp] = eKeyPageUp;
	io.KeyMap[ImGuiKey_PageDown] = eKeyPageDown;
	io.KeyMap[ImGuiKey_Home] = eKeyHome;
	io.KeyMap[ImGuiKey_End] = eKeyEnd;
	io.KeyMap[ImGuiKey_Insert] = eKeyInsert;
	io.KeyMap[ImGuiKey_Delete] = eKeyDelete;
	io.KeyMap[ImGuiKey_Backspace] = eKeyBackspace;
	io.KeyMap[ImGuiKey_Space] = eKeySpace;
	io.KeyMap[ImGuiKey_Enter] = eKeyEnter;
	io.KeyMap[ImGuiKey_Escape] = eKeyEscape;
	io.KeyMap[ImGuiKey_KeyPadEnter] = eKeyEnter;
	io.KeyMap[ImGuiKey_A] = eKeyA;
	io.KeyMap[ImGuiKey_C] = eKeyC;
	io.KeyMap[ImGuiKey_V] = eKeyV;
	io.KeyMap[ImGuiKey_X] = eKeyX;
	io.KeyMap[ImGuiKey_Y] = eKeyY;
	io.KeyMap[ImGuiKey_Z] = eKeyZ;

	mMesh.topology() = vk::PrimitiveTopology::eTriangleList;
  
	unordered_map<VertexArrayObject::AttributeType, vector<VertexArrayObject::Attribute>> attributes;
	attributes[VertexArrayObject::AttributeType::ePosition].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, pos), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	attributes[VertexArrayObject::AttributeType::eTexcoord].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, uv ), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	attributes[VertexArrayObject::AttributeType::eColor   ].emplace_back(VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR8G8B8A8Unorm, (uint32_t)offsetof(ImDrawVert, col), vk::VertexInputRate::eVertex), Buffer::View<byte>{});
	mMesh.vertices() = make_shared<VertexArrayObject>(attributes);

	auto app = mNode.find_in_ancestor<Application>();
	app->OnUpdate.listen(mNode, bind_front(&Gui::new_frame, this), EventPriority::eFirst);
	app->OnUpdate.listen(mNode, bind(&Gui::make_geometry, this, std::placeholders::_1), EventPriority::eAlmostLast);
	
	#pragma region Inspector gui
	register_inspector_gui_fn<Instance>(&inspector_gui_fn);
	register_inspector_gui_fn<ShaderDatabase>(&inspector_gui_fn);
	register_inspector_gui_fn<ComputePipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<GraphicsPipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<Application>(&inspector_gui_fn);
	register_inspector_gui_fn<Mesh>(&inspector_gui_fn);

  enum AssetType {
    eEnvironmentMap,
    eGLTFScene,
    eMitsubaScene,
  };
  vector<tuple<fs::path, string, AssetType>> assets;
  for (const string& filepath : app->window().mInstance.find_arguments("assetsFolder"))
    for (const auto& entry : fs::recursive_directory_iterator(filepath)) {
      if (entry.path().extension() == ".gltf")
        assets.emplace_back(entry.path(), entry.path().filename().string(), AssetType::eGLTFScene);
      else if (entry.path().extension() == ".xml")
        assets.emplace_back(entry.path(), entry.path().filename().string(), AssetType::eMitsubaScene);
      else if (entry.path().extension() == ".hdr")
        assets.emplace_back(entry.path(), entry.path().filename().string(), AssetType::eEnvironmentMap);
    }

	ranges::sort(assets, ranges::less{}, [](const auto& a) { return get<AssetType>(a); });
  
	create_pipelines();

  app->OnUpdate.listen(mNode, [=](CommandBuffer& commandBuffer, float deltaTime) {
		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();
		
		if (ImGui::Begin("Utilities")) {
			static int tab = 0;
			static Node* selected = nullptr;

    	Profiler::on_gui();

			if (ImGui::Button("Assets")) tab = 0;
			ImGui::SameLine();
			if (ImGui::Button("Scene")) tab = 1;
			ImGui::SameLine();
			if (ImGui::Button("Inspector")) tab = 2;
			
			switch (tab) {
			case 0:
				for (const auto&[filepath, name, type] : assets) {
          string n = name;
          if (ranges::find_if(assets, [&](const auto& t) { return get<0>(t) != filepath && name == get<1>(t); }) != assets.end())
            n = filepath.parent_path().filename().string() + "/" + n;
					if (ImGui::Button(n.c_str()))
						switch (type) {
							case AssetType::eGLTFScene:
								load_gltf(app->node().make_child(name), commandBuffer, filepath);
								break;
							case AssetType::eMitsubaScene:
								load_mitsuba(app->node().make_child(name), commandBuffer, filepath);
								break;
							case AssetType::eEnvironmentMap: {
								app->node().make_child(filepath.stem().string()).make_component<Material>(load_environment(commandBuffer, filepath));
								break;
							}
						}
        }
				break;

			case 1:
				node_graph_gui_fn(mNode.root(), selected);
				break;

			case 2:
				if (!selected)
					ImGui::Text("Select a node from the Scene tab");
				else {
					ImGui::Text(selected->name().c_str());
					ImGui::SetNextItemWidth(40);
					if (ImGui::Button("X")) {
						if (app->window().input_state().pressed(KeyCode::eKeyShift))
							mNode.node_graph().erase_recurse(*selected);
						else
							mNode.node_graph().erase(*selected);
						selected = nullptr;
					} else {
						if (!selected->find<hlsl::TransformData>() && ImGui::Button("Add Transform"))
							selected->make_component<hlsl::TransformData>(make_transform(float3::Zero(), quatf_identity(), float3::Ones()));
						type_index to_erase = typeid(nullptr_t);
						for (type_index type : selected->components()) {
							if (ImGui::CollapsingHeader(type.name())) {
								ImGui::SetNextItemWidth(40);
								if (ImGui::Button("X"))
									to_erase = type;
								else {
									void* ptr = selected->find(type);
									auto it = mInspectorGuiFns.find(type);
									if (it != mInspectorGuiFns.end())
										it->second(ptr);
								}
							}
						}
						if (to_erase != typeid(nullptr_t)) selected->erase_component(to_erase);
					}
				}
			}
    }
		ImGui::End();
  });
	#pragma endregion
}
Gui::~Gui() {
	ImGui::DestroyContext(mContext);
}

void Gui::create_pipelines() {
	const ShaderDatabase& shader = *mNode.node_graph().find_components<ShaderDatabase>().front();
	const auto& color_image_fs = shader.at("raster_color_image_fs");
	if (mPipeline) mNode.node_graph().erase(mPipeline.node());
	mPipeline = mNode.make_child("Pipeline").make_component<GraphicsPipelineState>("Gui", shader.at("raster_color_image_vs"), color_image_fs);
	mPipeline->raster_state().setFrontFace(vk::FrontFace::eClockwise);
	mPipeline->depth_stencil().setDepthTestEnable(false);
	mPipeline->depth_stencil().setDepthWriteEnable(false);
	mPipeline->blend_states() = { vk::PipelineColorBlendAttachmentState(true,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA) };
	mPipeline->set_immutable_sampler("gSampler", make_shared<Sampler>(color_image_fs->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));
	
	mPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
}

void Gui::create_font_image(CommandBuffer& commandBuffer) {
	unsigned char* pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	Buffer::View<byte> staging = make_shared<Buffer>(commandBuffer.mDevice, "ImGuiNode::CreateImages/Staging", width*height*texel_size(vk::Format::eR8G8B8A8Unorm), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
	memcpy(staging.data(), pixels, staging.size_bytes());
	Image::View img = commandBuffer.copy_buffer_to_image(staging, make_shared<Image>(commandBuffer.mDevice, "Gui/Image", vk::Extent3D(width, height, 1), vk::Format::eR8G8B8A8Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage));
	mPipeline->descriptor("gImages", 0) = image_descriptor(img, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
}

void Gui::new_frame(CommandBuffer& commandBuffer, float deltaTime) {
	ProfilerRegion ps("Update Gui");

	set_context();
	ImGuiIO& io = ImGui::GetIO();
	mImageMap.clear();

	Descriptor& imagesDescriptor = mPipeline->descriptor("gImages", 0);
	if (imagesDescriptor.index() != 0 || !get<Image::View>(imagesDescriptor))
		create_font_image(commandBuffer);
	
	Window& window = commandBuffer.mDevice.mInstance.window();
	io.DisplaySize = ImVec2((float)window.swapchain_extent().width, (float)window.swapchain_extent().height);
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
	io.DeltaTime = deltaTime;
	
	const MouseKeyboardState& input = window.input_state();
	io.MousePos = ImVec2(input.cursor_pos().x(), input.cursor_pos().y());
	io.MouseWheel = input.scroll_delta();
	io.MouseDown[0] = input.pressed(KeyCode::eMouse1);
	io.MouseDown[1] = input.pressed(KeyCode::eMouse2);
	io.MouseDown[2] = input.pressed(KeyCode::eMouse3);
	io.MouseDown[3] = input.pressed(KeyCode::eMouse4);
	io.MouseDown[4] = input.pressed(KeyCode::eMouse5);
	io.KeyCtrl = input.pressed(KeyCode::eKeyControl);
	io.KeyShift = input.pressed(KeyCode::eKeyShift);
	io.KeyAlt = input.pressed(KeyCode::eKeyAlt);
	ranges::uninitialized_fill(io.KeysDown, 0);
	for (KeyCode key : input.buttons())
		io.KeysDown[size_t(key)] = 1;
	io.AddInputCharactersUTF8(input.input_characters().c_str());

	ImGui::NewFrame();
}

void Gui::make_geometry(CommandBuffer& commandBuffer) {
	ProfilerRegion ps("Render Gui");
	set_context();
	ImGui::Render();

	mDrawData = ImGui::GetDrawData();
	if (mDrawData && mDrawData->TotalVtxCount) {
		buffer_vector<ImDrawVert> vertices(commandBuffer.mDevice, mDrawData->TotalVtxCount, vk::BufferUsageFlagBits::eVertexBuffer);
		buffer_vector<ImDrawIdx>  indices (commandBuffer.mDevice, mDrawData->TotalIdxCount, vk::BufferUsageFlagBits::eIndexBuffer);
		auto dstVertex = vertices.begin();
		auto dstIndex  = indices.begin();
		for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
			ranges::copy(cmdList->VtxBuffer, dstVertex);
			ranges::copy(cmdList->IdxBuffer, dstIndex);
			dstVertex += cmdList->VtxBuffer.size();
			dstIndex  += cmdList->IdxBuffer.size();
			for (const ImDrawCmd& cmd : cmdList->CmdBuffer) {
				if (cmd.TextureId != nullptr) {
					Image::View& view = *reinterpret_cast<Image::View*>(cmd.TextureId);
					if (!mImageMap.contains(view)) {
						uint32_t idx = 1 + (uint32_t)mImageMap.size();
						mPipeline->descriptor("gImages", idx) = image_descriptor(view, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
						mImageMap.emplace(view, idx);
					}
				}
			}
		}
		
		mMesh[VertexArrayObject::AttributeType::ePosition][0].second = vertices.buffer_view();
		mMesh[VertexArrayObject::AttributeType::eTexcoord][0].second = vertices.buffer_view();
		mMesh[VertexArrayObject::AttributeType::eColor   ][0].second = vertices.buffer_view();
		mMesh.indices() = indices.buffer_view();

		Descriptor& imagesDescriptor = mPipeline->descriptor("gImages", 0);
		if (imagesDescriptor.index() != 0 || !get<Image::View>(imagesDescriptor))
			create_font_image(commandBuffer);

		mPipeline->transition_images(commandBuffer);
	}
}
void Gui::render(CommandBuffer& commandBuffer, const Image::View& dst) {
	if (!mDrawData || mDrawData->CmdListsCount <= 0 || mDrawData->DisplaySize.x == 0 || mDrawData->DisplaySize.y == 0) return;

	ProfilerRegion ps("Draw Gui", commandBuffer);

	RenderPass::SubpassDescription subpass {
		{ "colorBuffer", {
			AttachmentType::eColor, blend_mode_state(), vk::AttachmentDescription{ {},
				dst.image()->format(), dst.image()->sample_count(),
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal } }
		}
	};
  auto renderPass = make_shared<RenderPass>(dst.image()->mDevice, "Gui RenderPass", ranges::single_view { subpass });
  auto framebuffer = make_shared<Framebuffer>(*renderPass, "Gui Framebuffer", ranges::single_view { dst });
  commandBuffer.begin_render_pass(renderPass, framebuffer, vk::Rect2D{ {}, framebuffer->extent() }, { {} });

	float2 scale = float2::Map(&mDrawData->DisplaySize.x);
	float2 offset = float2::Map(&mDrawData->DisplayPos.x);
	
	Buffer::View<hlsl::ViewData> views = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(hlsl::ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	views[0].world_to_camera = views[0].camera_to_world = make_transform(float3(0,0,1), quatf_identity(), float3::Ones());
	views[0].projection = make_orthographic(scale, -1 - offset.array()*2/scale.array(), 0, 1);
	views[0].image_min = { 0, 0 };
	views[0].image_max = { framebuffer->extent().width, framebuffer->extent().height };

	mPipeline->descriptor("gViews") = commandBuffer.hold_resource(views);
	mPipeline->push_constant<uint32_t>("gViewIndex") = 0;
	mPipeline->push_constant<float4>("gImageST") = float4(1,1,0,0);
	mPipeline->push_constant<float4>("gColor") = float4::Ones();
	
	commandBuffer.bind_pipeline(mPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), mMesh.vertex_layout(*mPipeline->stage(vk::ShaderStageFlagBits::eVertex))));
	mPipeline->bind_descriptor_sets(commandBuffer);
	mPipeline->push_constants(commandBuffer);
	mMesh.bind(commandBuffer);

	commandBuffer->setViewport(0, vk::Viewport(mDrawData->DisplayPos.x, mDrawData->DisplayPos.y, mDrawData->DisplaySize.x, mDrawData->DisplaySize.y, 0, 1));
	
	uint32_t voff = 0, ioff = 0;
	for (const ImDrawList* cmdList : span(mDrawData->CmdLists, mDrawData->CmdListsCount)) {
		for (const ImDrawCmd& cmd : cmdList->CmdBuffer)
			if (cmd.UserCallback) {
				// TODO: reset render state callback
				// if (cmd->UserCallback == ResetRenderState)
				cmd.UserCallback(cmdList, &cmd);
			} else {
				commandBuffer.push_constant("gImageIndex", cmd.TextureId ? mImageMap.at(*reinterpret_cast<Image::View*>(cmd.TextureId)) : 0);
				vk::Offset2D offset((int32_t)cmd.ClipRect.x, (int32_t)cmd.ClipRect.y);
				vk::Extent2D extent((uint32_t)(cmd.ClipRect.z - cmd.ClipRect.x), (uint32_t)(cmd.ClipRect.w - cmd.ClipRect.y));
				commandBuffer->setScissor(0, vk::Rect2D(offset, extent));
				commandBuffer->drawIndexed(cmd.ElemCount, 1, ioff + cmd.IdxOffset, voff + cmd.VtxOffset, 0);
			}
		voff += cmdList->VtxBuffer.size();
		ioff += cmdList->IdxBuffer.size();
	}

  commandBuffer.end_render_pass();
}