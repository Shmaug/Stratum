
#include "Inspector.hpp"
#include "Scene.hpp"
#include "Application.hpp"

namespace stm {

inline void inspector_gui_fn(Instance* instance) {
	ImGui::Text("Vulkan %u.%u.%u",
		VK_API_VERSION_MAJOR(instance->vulkan_version()),
		VK_API_VERSION_MINOR(instance->vulkan_version()),
		VK_API_VERSION_PATCH(instance->vulkan_version()));

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
inline void inspector_gui_fn(GraphicsPipelineState* pipeline) {
	ImGui::Text("%lu pipelines", pipeline->pipelines().size());
	ImGui::Text("%lu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(ComputePipelineState* pipeline) {
	ImGui::Text("%lu pipelines", pipeline->pipelines().size());
	ImGui::Text("%lu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(Mesh* mesh) {
	ImGui::LabelText("Topology", to_string(mesh->topology()).c_str());
	ImGui::LabelText("Index Type", to_string(mesh->index_type()).c_str());
	if (mesh->vertices()) {
		for (const auto& [type, verts] : *mesh->vertices())
			for (uint32_t i = 0; i < verts.size(); i++)
				if (verts[i].second && ImGui::CollapsingHeader((to_string(type) + "_" + to_string(i)).c_str())) {
					ImGui::LabelText("Format", to_string(verts[i].first.mFormat).c_str());
					ImGui::LabelText("Stride", to_string(verts[i].first.mStride).c_str());
					ImGui::LabelText("Offset", to_string(verts[i].first.mOffset).c_str());
					ImGui::LabelText("Input Rate", to_string(verts[i].first.mInputRate).c_str());
				}
	}
}
inline void inspector_gui_fn(nanovdb::GridHandle<nanovdb::HostBuffer>* grid) {
	const nanovdb::GridMetaData* metadata = grid->gridMetaData();
	ImGui::LabelText("grid name", metadata->shortGridName());
	ImGui::LabelText("grid count", "%u", metadata->gridCount());
	ImGui::LabelText("grid type", nanovdb::toStr(metadata->gridType()));
	ImGui::LabelText("grid class", nanovdb::toStr(metadata->gridClass()));
	ImGui::LabelText("bbox min", "%.02f %.02f %.02f", metadata->worldBBox().min()[0], metadata->worldBBox().min()[1], metadata->worldBBox().min()[2]);
	ImGui::LabelText("bbox max", "%.02f %.02f %.02f", metadata->worldBBox().max()[0], metadata->worldBBox().max()[1], metadata->worldBBox().max()[2]);
}

inline void node_graph_gui_fn(Node& n, Node*& selected) {
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_OpenOnArrow;
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

Inspector::Inspector(Node& node) : mNode(node) {
	register_inspector_gui_fn<Instance>(&inspector_gui_fn);
	register_inspector_gui_fn<ComputePipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<GraphicsPipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<Mesh>(&inspector_gui_fn);
	register_inspector_gui_fn<nanovdb::GridHandle<nanovdb::HostBuffer>>(&inspector_gui_fn);

	auto app = mNode.find_in_ancestor<Application>();

	app->OnUpdate.add_listener(mNode, [&, app](CommandBuffer& commandBuffer, float deltaTime) {
		ProfilerRegion ps("Inspector Gui");
		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();

		static Node* selected = nullptr;

		if (ImGui::Begin("Profiler")) {
			{
				ProfilerRegion ps("Profiler::frame_times_gui");
				Profiler::frame_times_gui();
			}
			static int profiler_n = 3;
			ImGui::SliderInt("Count", &profiler_n, 1, 256);
			ImGui::SameLine();
			if (ImGui::Button("Timeline"))
				Profiler::reset_history(profiler_n);
		}
		ImGui::End();

		if (!Profiler::history().empty()) {
			if (ImGui::Begin("Timeline")) {
				ProfilerRegion ps("Profiler::timeline_gui");
				Profiler::sample_timeline_gui();
			}
			ImGui::End();
		}

		if (ImGui::Begin("Node Graph"))
			node_graph_gui_fn(mNode.root(), selected);
		ImGui::End();

		if (ImGui::Begin("Inspector")) {
			if (selected) {
				bool del = ImGui::Button("X");
				ImGui::SameLine();
				ImGui::Text(selected->name().c_str());
				ImGui::SetNextItemWidth(40);

				if (del) {
					if (app->window().input_state().pressed(KeyCode::eKeyShift))
						mNode.node_graph().erase_recurse(*selected);
					else
						mNode.node_graph().erase(*selected);
					selected = nullptr;
				} else {
					if (!selected->find<TransformData>() && ImGui::Button("Add Transform"))
						selected->make_component<TransformData>(make_transform(float3::Zero(), quatf_identity(), float3::Ones()));
					type_index to_erase = typeid(nullptr_t);
					for (type_index type : selected->components()) {
						bool d = ImGui::Button("X");
						ImGui::SameLine();
						if (ImGui::CollapsingHeader(type.name())) {
							auto it = mInspectorGuiFns.find(type);
							if (it != mInspectorGuiFns.end()) {
								ImGui::Indent();
								it->second(selected->find(type));
								ImGui::Unindent();
							}
						}
						if (d) to_erase = type;
					}
					if (to_erase != typeid(nullptr_t)) selected->erase_component(to_erase);
				}
			} else
				ImGui::Text("Select a node to inspect");
		}
		ImGui::End();
	}, Node::EventPriority::eAlmostFirst);
}

}