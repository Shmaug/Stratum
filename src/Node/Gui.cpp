#include "Gui.hpp"
#include "Application.hpp"
#include "RasterScene.hpp"
#include "RayTraceScene.hpp"

#include <imgui_internal.h>
#include <stb_image_write.h>

#include <Core/Window.hpp>

namespace stm {
namespace hlsl {
#include <HLSL/transform.hlsli>
}
}

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
inline void inspector_gui_fn(Application* app) {
	if (ImGui::Button("Reload Shaders")) {
		app->window().mInstance.device().flush();
		app->load_shaders();
		app->node().for_each_descendant<Gui>([](const auto& v) { v->create_pipelines();	});
		app->node().for_each_descendant<RasterScene>([](const auto& v) { v->create_pipelines();	});
		app->node().for_each_descendant<RayTraceScene>([](const auto& v) { v->create_pipelines();	});
	}
	
  if (ImGui::BeginCombo("Main Camera", app->main_camera().node().name().c_str())) {
    app->node().for_each_descendant<Camera>([&](component_ptr<Camera> c) {
      if (ImGui::Selectable(c.node().name().c_str(), app->main_camera() == c))
        app->main_camera(c);
    });
    ImGui::EndCombo();
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
  ImGui::LabelText("Window format", to_string(instance->window().surface_format().format).c_str());
  ImGui::LabelText("Window color space", to_string(instance->window().surface_format().colorSpace).c_str());
}
inline void inspector_gui_fn(ShaderDatabase* shader) {
  for (const auto&[name, spv] : *shader) {
    ImGui::LabelText(name.c_str(), "%s | %s", spv->entry_point().c_str(), to_string(spv->stage()).c_str());
  }
}
inline void inspector_gui_fn(DynamicRenderPass* rp) {
  for (auto& sp : rp->subpasses()) {
    if (ImGui::BeginListBox(sp->name().c_str())) {
      for (auto&[attachment, a] : sp->description().attachments()) {
          ImGui::Text(attachment.c_str());
          const char* items[] { "Input", "Color", "Resolve", "DepthStencil", "Preserve" };
          if (ImGui::BeginCombo("Type", items[a.mType])) {
            for (uint32_t i = 0; i < ranges::size(items); i++)
              if (ImGui::Selectable(items[i], a.mType==i))
                a.mType = (AttachmentType)i;
            ImGui::EndCombo();
          }
      }
      ImGui::EndListBox();
    }
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
	if (mesh->vertices())
		for (const auto&[type,verts] : *mesh->vertices())
			for (uint32_t i = 0; i < verts.size(); i++)
				if (verts[i].second && ImGui::CollapsingHeader((to_string(type) + "_" + to_string(i)).c_str())) {
					ImGui::LabelText("Format", to_string(verts[i].first.mFormat).c_str());
					ImGui::LabelText("Stride", to_string(verts[i].first.mStride).c_str());
					ImGui::LabelText("Offset", to_string(verts[i].first.mOffset).c_str());
					ImGui::LabelText("Input Rate", to_string(verts[i].first.mInputRate).c_str());
				}
}

inline void node_graph_gui_fn(Node& n, Node*& selected) {
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick|ImGuiTreeNodeFlags_OpenOnArrow;
  if (&n == selected) flags |= ImGuiTreeNodeFlags_Selected;
  if (n.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx(n.name().c_str(), flags)) {
    if (ImGui::IsItemClicked())
      selected = &n;
    for (Node& c : n.children())
      node_graph_gui_fn(c, selected);
    ImGui::TreePop();
  }
}
#pragma endregion

Gui::Gui(Node& node) : mNode(node) {
	mContext = ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	
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
	mMesh.vertices() = make_shared<VertexArrayObject>(VertexArrayObject::AttributeType::ePosition, VertexArrayObject::AttributeType::eTexcoord, VertexArrayObject::AttributeType::eColor);
	mMesh[VertexArrayObject::AttributeType::ePosition][0].first = VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, pos), vk::VertexInputRate::eVertex);
	mMesh[VertexArrayObject::AttributeType::eTexcoord][0].first = VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR32G32Sfloat,  (uint32_t)offsetof(ImDrawVert, uv ), vk::VertexInputRate::eVertex);
	mMesh[VertexArrayObject::AttributeType::eColor   ][0].first = VertexArrayObject::AttributeDescription(sizeof(ImDrawVert), vk::Format::eR8G8B8A8Unorm, (uint32_t)offsetof(ImDrawVert, col), vk::VertexInputRate::eVertex);

	auto app = mNode.find_in_ancestor<Application>();
  
	mRenderNode = mNode.make_child(node.name() + " DynamicRenderPass").make_component<DynamicRenderPass>();
  mRenderPass = mRenderNode->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mRenderNode, "renderPass"));
  mRenderPass->emplace_attachment("colorBuffer", AttachmentType::eColor, blend_mode_state(),
    vk::AttachmentDescription({},
      app->main_pass()->attachment("colorBuffer").mDescription.format, vk::SampleCountFlagBits::e1,
      vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR));
	
	app->OnUpdate.listen(mNode, bind_front(&Gui::new_frame, this), EventPriority::eFirst + 16);
	app->OnUpdate.listen(mNode, bind(&Gui::make_geometry, this, std::placeholders::_1), EventPriority::eLast - 16);
	mRenderPass->OnDraw.listen(mNode, bind_front(&Gui::draw, this));
	
	// draw after app's render_pass
	app->main_pass()->mPass.PostProcess.listen(mNode, [&](CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
		mRenderNode->render(commandBuffer, { { "colorBuffer", { framebuffer->at("colorBuffer"), {} } } } );
	}, EventPriority::eLast - 16);
	
	#pragma region Inspector gui
	register_inspector_gui_fn<Instance>(&inspector_gui_fn);
	register_inspector_gui_fn<ShaderDatabase>(&inspector_gui_fn);
	register_inspector_gui_fn<DynamicRenderPass>(&inspector_gui_fn);
	register_inspector_gui_fn<ComputePipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<GraphicsPipelineState>(&inspector_gui_fn);
	register_inspector_gui_fn<Application>(&inspector_gui_fn);
	register_inspector_gui_fn<Mesh>(&inspector_gui_fn);

  enum AssetType {
    eEnvironmentMap,
    eScene
  };
  vector<tuple<fs::path, string, AssetType>> assets;
  for (const string& filepath : app->window().mInstance.find_arguments("assetsFolder"))
    for (const auto& entry : fs::recursive_directory_iterator(filepath)) {
      if (entry.path().extension() == ".gltf")
        assets.emplace_back(entry.path(), entry.path().filename().string(), AssetType::eScene);
      else if (entry.path().extension() == ".hdr")
        assets.emplace_back(entry.path(), entry.path().filename().string(), AssetType::eEnvironmentMap);
    }

	ranges::sort(assets, ranges::less{}, [](const auto& a) { return get<AssetType>(a); });
  
	create_pipelines();

  app->OnUpdate.listen(mNode, [=](CommandBuffer& commandBuffer, float deltaTime) {
		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();
		
    Profiler::on_gui();
    
    if (ImGui::Begin("Load Asset")) {
      for (const auto&[filepath, name, type] : assets)
        if (ImGui::Button(name.c_str()))
          switch (type) {
            case AssetType::eScene:
              load_gltf(app->node().make_child(name), commandBuffer, filepath);
              break;
            case AssetType::eEnvironmentMap: {
              auto[pixels,extent] = Image::load(commandBuffer.mDevice, filepath);
							auto envMap = app->node().find_in_descendants<EnvironmentMap>();
							if (!envMap) envMap = app->node().make_child("Environment Map").make_component<EnvironmentMap>();
              envMap->mImage = make_shared<Image>(commandBuffer.mDevice, name, extent, pixels.format(), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled);
              commandBuffer.copy_buffer_to_image(pixels, envMap->mImage);
              envMap->mGamma = 2.2f;

							if (pixels.format() == vk::Format::eR32G32B32A32Sfloat) {
								string marginalPath = filepath.string() + ".marginal";
								string conditionalPath = filepath.string() + ".conditional";
								Buffer::View<float2> marginalDistData = make_shared<Buffer>(commandBuffer.mDevice, "marginalDistData", extent.height*sizeof(float2), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
								Buffer::View<float2> conditionalDistData = make_shared<Buffer>(commandBuffer.mDevice, "conditionalDistData", extent.width*extent.height*sizeof(float2), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);

								if (fs::exists(marginalPath) && fs::exists(conditionalPath)) { 
									read_file(marginalPath, marginalDistData);
									read_file(conditionalPath, conditionalDistData);
								} else {
									EnvironmentMap::build_distributions(
										span<float4>(reinterpret_cast<float4*>(pixels.data()), pixels.size()/sizeof(float4)), vk::Extent2D(extent.width, extent.height),
										span(marginalDistData.data(), marginalDistData.size()),
										span(conditionalDistData.data(), conditionalDistData.size()));
									write_file(marginalPath, marginalDistData);
									write_file(conditionalPath, conditionalDistData);
								}

								envMap->mMarginalDistribution = Image::View(
									make_shared<Image>(commandBuffer.mDevice, name, vk::Extent3D(extent.height,1,1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled), 
									0, 1, 0, 1, {}, {}, vk::ImageViewType::e2D);
								envMap->mConditionalDistribution = make_shared<Image>(commandBuffer.mDevice, name, vk::Extent3D(extent.width, extent.height, 1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled);
								commandBuffer.copy_buffer_to_image(marginalDistData, envMap->mMarginalDistribution);
								commandBuffer.copy_buffer_to_image(conditionalDistData, envMap->mConditionalDistribution);
							}
              break;
            }
          }
    }
    ImGui::End();
    
    // scenegraph inspector
    if (ImGui::Begin("Scene Graph")) {
      ImGui::Columns(2);
			
			static Node* selected = nullptr;
			if (ImGui::BeginChild("Scene Graph", ImVec2(), false, ImGuiWindowFlags_NoTitleBar))
      	node_graph_gui_fn(mNode.root(), selected);
			ImGui::EndChild();
      ImGui::NextColumn();
      if (selected) {
        ImGui::Text(selected->name().c_str());
				if (ImGui::Button("Delete Node")) {
					if (app->window().input_state().pressed(KeyCode::eKeyShift))
						mNode.node_graph().erase_recurse(*selected);
					else
						mNode.node_graph().erase(*selected);
					selected = nullptr;
				} else {
					type_index to_erase = typeid(nullptr_t);
					for (type_index type : selected->components()) {
						if (ImGui::CollapsingHeader(type.name())) {
							if (ImGui::Button("Delete Component"))
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
    ImGui::End();
  });
	#pragma endregion
}
Gui::~Gui() {
	ImGui::DestroyContext(mContext);
}

void Gui::create_pipelines() {
	const ShaderDatabase& shader = *mNode.node_graph().find_components<ShaderDatabase>().front();
	const auto& basic_color_image_fs = shader.at("basic_color_image_fs");
	if (mPipeline) mNode.node_graph().erase(mPipeline.node());
	mPipeline = mNode.make_child("Pipeline").make_component<GraphicsPipelineState>("Gui", shader.at("basic_color_image_vs"), basic_color_image_fs);
	mPipeline->raster_state().setFrontFace(vk::FrontFace::eClockwise);
	mPipeline->depth_stencil().setDepthTestEnable(false);
	mPipeline->depth_stencil().setDepthWriteEnable(false);
	mPipeline->blend_states() = { vk::PipelineColorBlendAttachmentState(true,
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, 
		vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA) };
	mPipeline->set_immutable_sampler("gSampler", make_shared<Sampler>(basic_color_image_fs->mDevice, "gSampler", vk::SamplerCreateInfo({},
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
void Gui::draw(CommandBuffer& commandBuffer) {
	if (!mDrawData || mDrawData->CmdListsCount <= 0) return;

	ProfilerRegion ps("Draw Gui", commandBuffer);

	float2 scale = float2::Map(&mDrawData->DisplaySize.x);
	float2 offset = float2::Map(&mDrawData->DisplayPos.x);
	
	#pragma pack(push)
	#pragma pack(1)
	struct CameraData {
		TransformData gWorldToCamera;
		TransformData gCameraToWorld;
		ProjectionData gProjection;
	};
	#pragma pack(pop)
	Buffer::View<CameraData> cameraData = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	cameraData[0].gWorldToCamera = cameraData[0].gCameraToWorld = make_transform(float3(0,0,1), make_quatf(0,0,0,1), float3::Ones());
	cameraData[0].gProjection = make_orthographic(scale, -1 - offset.array()*2/scale.array(), 0, 1);

	mPipeline->descriptor("gCameraData") = commandBuffer.hold_resource(cameraData);
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
}