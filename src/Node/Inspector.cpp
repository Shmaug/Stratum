
#include "Inspector.hpp"
#include "Scene.hpp"
#include "Application.hpp"
#include "RayTraceScene.hpp" // for create_pipelines
#include <Common/CDialogEventHandler.hpp>

using namespace stm;
using namespace stm::hlsl;

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

Inspector::Inspector(Node& node) : mNode(node) {
    register_inspector_gui_fn<Instance>(&inspector_gui_fn);
    register_inspector_gui_fn<ShaderDatabase>(&inspector_gui_fn);
    register_inspector_gui_fn<ComputePipelineState>(&inspector_gui_fn);
    register_inspector_gui_fn<GraphicsPipelineState>(&inspector_gui_fn);
    register_inspector_gui_fn<Application>(&inspector_gui_fn);
    register_inspector_gui_fn<Mesh>(&inspector_gui_fn);
    register_inspector_gui_fn<nanovdb::GridHandle<nanovdb::HostBuffer>>(&inspector_gui_fn);

    enum ImporterType {
        eEnvironmentMap,
        eMitsubaScene,
        eGLTF,
        eAssimp,
        eVolume,
        eNVDBVolume,
        eVDBVolume
    };
    static const unordered_map<string, ImporterType> extensionMap {
        { ".hdr", eEnvironmentMap },
        { ".exr", eEnvironmentMap },
        { ".xml", eMitsubaScene },
        { ".gltf", eGLTF },
        { ".glb", eGLTF },

        { ".fbx", eAssimp },
        { ".obj", eAssimp },
        { ".blend", eAssimp },
        { ".ply", eAssimp },
        { ".stl", eAssimp },

        { ".vol", eVolume },
        { ".nvdb", eNVDBVolume },
        { ".vdb", eVDBVolume },
    };

	auto app = mNode.find_in_ancestor<Application>();

    /*
    vector<tuple<fs::path, string, AssetType>> assets;
    for (const string& filepath : app->window().mInstance.find_arguments("assetsFolder"))
        for (const auto& entry : fs::recursive_directory_iterator(filepath))
            if (auto it = extensionMap.find(entry.path().extension().string()); it != extensionMap.end())
                assets.emplace_back(entry.path(), entry.path().filename().string(), it->second);
    ranges::sort(assets, ranges::less{}, [](const auto& a) { return get<AssetType>(a); });
    */

    app->OnUpdate.listen(mNode, [=](CommandBuffer& commandBuffer, float deltaTime) {
        ProfilerRegion ps("Inspector Gui");
        auto gui = app.node().find_in_descendants<Gui>();
        if (!gui) return;
        gui->set_context();

        static Node* selected = nullptr;

        if (ImGui::Begin("Scene")) {
            if (ImGui::Button("Load File")) {
                const fs::path filepath = file_dialog(app->window().handle());
                const string name = filepath.filename().string();
                auto it = extensionMap.find(filepath.extension().string().c_str());
                if (it != extensionMap.end()) {
                    switch (it->second) {
                    case ImporterType::eEnvironmentMap:
                        app->node().make_child(name).make_component<Material>(load_environment(commandBuffer, filepath));
                        break;
                    case ImporterType::eMitsubaScene:
                        load_mitsuba(app->node().make_child(name), commandBuffer, filepath);
                        break;
                    case ImporterType::eGLTF:
                        load_gltf(app->node().make_child(name), commandBuffer, filepath);
                        break;
    #ifdef STRATUM_ENABLE_ASSIMP
                    case ImporterType::eAssimp:
                        load_assimp(app->node().make_child(name), commandBuffer, filepath);
                        break;
    #endif
                    case ImporterType::eVolume:
                        load_vol(app->node().make_child(name), commandBuffer, filepath);
                        break;
                    case ImporterType::eNVDBVolume:
                        load_nvdb(app->node().make_child(name), commandBuffer, filepath);
                        break;
    #ifdef STRATUM_ENABLE_OPENVDB
                    case ImporterType::eVDBVolume:
                        load_vdb(app->node().make_child(name), commandBuffer, filepath);
                        break;
    #endif
                    }
                }
            }
            node_graph_gui_fn(mNode.root(), selected);
        }
        ImGui::End();

        if (ImGui::Begin("Inspector")) {
            if (selected) {
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
            } else
                ImGui::Text("Select a node to inspect");
        }
        ImGui::End();
    }, EventPriority::eAlmostFirst);
}