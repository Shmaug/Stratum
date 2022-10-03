
#include "Inspector.hpp"
#include "Scene.hpp"
#include "Application.hpp"

namespace stm {

inline void inspector_gui_fn(Inspector& inspector, Instance* instance) {
	ImGui::Text("Vulkan %u.%u.%u",
		VK_API_VERSION_MAJOR(instance->vulkan_version()),
		VK_API_VERSION_MINOR(instance->vulkan_version()),
		VK_API_VERSION_PATCH(instance->vulkan_version()));

	ImGui::Text("%u Descriptor sets", instance->device().descriptor_set_count());

	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(instance->device().allocator(), budgets);
	const vk::PhysicalDeviceMemoryProperties properties = instance->device().physical().getMemoryProperties();
	for (uint32_t heapIndex = 0; heapIndex < properties.memoryHeapCount; heapIndex++) {
		const auto[usage, usageUnit] = format_bytes(budgets[heapIndex].usage);
		const auto[budget, budgetUnit] = format_bytes(budgets[heapIndex].budget);
		const auto[allocationBytes, allocationBytesUnit] = format_bytes(budgets[heapIndex].statistics.allocationBytes);
		const auto[blockBytes, blockBytesUnit] = format_bytes(budgets[heapIndex].statistics.blockBytes);
		ImGui::Text("Heap %u %s", heapIndex, (properties.memoryHeaps[heapIndex].flags & vk::MemoryHeapFlagBits::eDeviceLocal) ? "(device local)" : "");
		ImGui::Text("%llu %s used, %llu %s budgeted", usage, usageUnit, budget, budgetUnit);
		ImGui::Indent();
		ImGui::Text("%u allocations (%llu %s)", budgets[heapIndex].statistics.allocationCount, allocationBytes, allocationBytesUnit);
		ImGui::Text("%u device memory blocks (%llu %s)", budgets[heapIndex].statistics.blockCount, blockBytes, blockBytesUnit);
		ImGui::Unindent();
	}

	vk::Extent2D swapchain_extent = instance->window().swapchain_extent();
	bool resize = false;
	ImGui::InputScalar("Swapchain width", ImGuiDataType_U32, &swapchain_extent.width);
	resize |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::InputScalar("Swapchain height", ImGuiDataType_U32, &swapchain_extent.height);
	resize |= ImGui::IsItemDeactivatedAfterEdit();
	if (resize) instance->window().resize(swapchain_extent);

	ImGui::LabelText("Render target format", to_string(instance->window().back_buffer().image()->format()).c_str());
	ImGui::LabelText("Image count", to_string(instance->window().back_buffer_count()).c_str());

	vk::SurfaceCapabilitiesKHR capabilities = instance->device().physical().getSurfaceCapabilitiesKHR(instance->window().surface());
	int c = instance->window().min_image_count();
	ImGui::SetNextItemWidth(40);
	if (ImGui::DragInt("Min image count", &c, 1, capabilities.minImageCount, capabilities.maxImageCount))
		instance->window().min_image_count(c);

	if (ImGui::BeginCombo("Present mode", to_string(instance->window().present_mode()).c_str())) {
		for (auto mode : instance->device().physical().getSurfacePresentModesKHR(instance->window().surface()))
			if (ImGui::Selectable(to_string(mode).c_str(), instance->window().present_mode() == mode))
				instance->window().preferred_present_mode(mode);
		ImGui::EndCombo();
	}

	auto fmt_to_str = [](vk::SurfaceFormatKHR f) { return to_string(f.format) + ", " + to_string(f.colorSpace); };
	if (ImGui::BeginCombo("Surface format", fmt_to_str(instance->window().surface_format()).c_str())) {
		for (auto format : instance->device().physical().getSurfaceFormatsKHR(instance->window().surface()))
			if (ImGui::Selectable(fmt_to_str(format).c_str(), instance->window().surface_format() == format))
				instance->window().preferred_surface_format(format);
		ImGui::EndCombo();
	}

	int64_t timeout = instance->window().acquire_image_timeout().count();
	if (ImGui::InputScalar("Swapchain image timeout (ns)", ImGuiDataType_U64, &timeout))
		instance->window().acquire_image_timeout(chrono::nanoseconds(timeout));
}
inline void inspector_gui_fn(Inspector& inspector, GraphicsPipelineState* pipeline) {
	ImGui::Text("%lu pipelines", pipeline->pipelines().size());
	ImGui::Text("%lu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(Inspector& inspector, ComputePipelineState* pipeline) {
	ImGui::Text("%lu pipelines", pipeline->pipelines().size());
	ImGui::Text("%lu descriptor sets", pipeline->descriptor_sets().size());
}
inline void inspector_gui_fn(Inspector& inspector, Mesh* mesh) {
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
inline void inspector_gui_fn(Inspector& inspector, nanovdb::GridHandle<nanovdb::HostBuffer>* grid) {
	const nanovdb::GridMetaData* metadata = grid->gridMetaData();
	ImGui::LabelText("grid name", metadata->shortGridName());
	ImGui::LabelText("grid count", "%u", metadata->gridCount());
	ImGui::LabelText("grid type", nanovdb::toStr(metadata->gridType()));
	ImGui::LabelText("grid class", nanovdb::toStr(metadata->gridClass()));
	ImGui::LabelText("bbox min", "%.02f %.02f %.02f", metadata->worldBBox().min()[0], metadata->worldBBox().min()[1], metadata->worldBBox().min()[2]);
	ImGui::LabelText("bbox max", "%.02f %.02f %.02f", metadata->worldBBox().max()[0], metadata->worldBBox().max()[1], metadata->worldBBox().max()[2]);
}

void Inspector::node_graph_gui_fn(Node& n) {
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_OpenOnArrow;
	if (&n == mSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (n.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;

	if (mSelected && mSelected->descendant_of(n))
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);

	if (ImGui::TreeNodeEx(n.name().c_str(), flags)) {
		if (ImGui::IsItemClicked())
			select(&n);
		for (Node& c : n.children())
			node_graph_gui_fn(c);
		ImGui::TreePop();
	}
}

Inspector::Inspector(Node& node) : mNode(node), mSelected(nullptr) {
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

		if (ImGui::Begin("Profiler")) {
			{
				ProfilerRegion ps("Profiler::frame_times_gui");
				Profiler::frame_times_gui();
			}
			static int profiler_n = 3;
			ImGui::SliderInt("Count", &profiler_n, 1, 256);
			ImGui::SameLine();
			if (ImGui::Button("Timeline"))
				Profiler::reset_history(Profiler::has_history() ? 0 : profiler_n);
		}
		ImGui::End();

		if (Profiler::has_history()) {
			if (ImGui::Begin("Timeline")) {
				ProfilerRegion ps("Profiler::sample_timeline_gui");
				Profiler::sample_timeline_gui();
			}
			ImGui::End();
		}

		if (ImGui::Begin("Node Graph"))
			node_graph_gui_fn(mNode.root());
		ImGui::End();

		if (ImGui::Begin("Inspector")) {
			if (mSelected) {
				const bool del = ImGui::Button("x");
				ImGui::SameLine();
				ImGui::Text(mSelected->name().c_str());
				ImGui::SetNextItemWidth(40);

				if (del) {
					for (auto scene : mNode.node_graph().find_components<Scene>())
						scene->mark_dirty();
					if (ImGui::GetIO().KeyAlt)
						mNode.node_graph().erase_recurse(*mSelected);
					else
						mNode.node_graph().erase(*mSelected);
					select(nullptr);
				} else {
					if (!mSelected->find<TransformData>() && ImGui::Button("Add Transform"))
						mSelected->make_component<TransformData>(make_transform(float3::Zero(), quatf_identity(), float3::Ones()));
					type_index to_erase = typeid(nullptr_t);
					for (type_index type : mSelected->components()) {
						ImGui::PushID(type.hash_code());
						const bool d = ImGui::Button("x");
						ImGui::PopID();
						ImGui::SameLine();
						if (ImGui::CollapsingHeader(type.name())) {
							auto it = mInspectorGuiFns.find(type);
							if (it != mInspectorGuiFns.end()) {
								ImGui::Indent();
								ImGui::Indent();
								it->second(*this, mSelected->find(type));
								ImGui::Unindent();
								ImGui::Unindent();
							}
						}
						if (d) to_erase = type;
					}
					if (to_erase != typeid(nullptr_t)) {
						mSelected->erase_component(to_erase);
						for (auto scene : mNode.node_graph().find_components<Scene>())
							scene->mark_dirty();
					}
				}
			} else
				ImGui::Text("Select a node to inspect");
		}
		ImGui::End();
	}, Node::EventPriority::eAlmostFirst);
}

}