#include "RasterScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm;
using namespace stm::hlsl;

#pragma region inspector gui + gizmo functions
template<typename T> inline void type_gizmo_fn(RasterScene::PushGeometry&, T&) {}
template<> inline void type_gizmo_fn(RasterScene::PushGeometry& pg, LightData& light) {
  pg.quad(light.mLightToWorld.Translation, 0, float2::Ones(), float4(light.mEmission[0], light.mEmission[1], light.mEmission[2], 1.f));
}

template<typename T> inline void type_gui_fn(T&) {}
template<> inline void type_gui_fn(Instance& instance) {
  ImGui::Text("Vulkan %u.%u.%u", 
    VK_API_VERSION_MAJOR(instance.vulkan_version()),
    VK_API_VERSION_MINOR(instance.vulkan_version()),
    VK_API_VERSION_PATCH(instance.vulkan_version()) );

  VmaStats stats;
  vmaCalculateStats(instance.device().allocator(), &stats);
  auto used = format_bytes(stats.total.usedBytes);
  auto unused = format_bytes(stats.total.usedBytes);
  ImGui::LabelText("Used memory", "%zu %s", used.first, used.second);
  ImGui::LabelText("Unused memory", "%zu %s", unused.first, unused.second);
  ImGui::LabelText("Device allocations", "%u", stats.total.blockCount);

  ImGui::LabelText("Window resolution", "%ux%u", instance.window().swapchain_extent().width, instance.window().swapchain_extent().height);
  ImGui::LabelText("Window format", to_string(instance.window().surface_format().format).c_str());
  ImGui::LabelText("Window color space", to_string(instance.window().surface_format().colorSpace).c_str());
}
template<> inline void type_gui_fn(spirv_module_map& spirv) {
  for (const auto&[name, spv] : spirv) {
    ImGui::LabelText(name.c_str(), "%s | %s", spv->entry_point().c_str(), to_string(spv->stage()).c_str());
  }
}
template<> inline void type_gui_fn(TransformData& t) {
  ImGui::DragFloat3("Translation", t.Translation.data(), .1f);
  ImGui::DragFloat("Scale", &t.Scale, .05f);
  if (ImGui::DragFloat4("Rotation (XYZW)", t.Rotation.xyz.data(), .1f, -1, 1))
    t.Rotation = normalize(t.Rotation);
}
template<> inline void type_gui_fn(LightData& light) {
  if (ImGui::BeginListBox("Light to world")) {
    type_gui_fn(light.mLightToWorld);
    ImGui::EndListBox();
  }

  ImGui::DragFloat3("Emission", light.mEmission.data());
  ImGui::DragFloat("Shadow bias", &light.mShadowBias, .1f, 0, 4);
  const char* items[] { "Distant", "Point", "Spot" };
  if (ImGui::BeginCombo("Type", items[light.mType])) {
    for (uint32_t i = 0; i < ranges::size(items); i++)
      if (ImGui::Selectable(items[i], light.mType==i))
        light.mType = i;
    ImGui::EndCombo();
  }
  ImGui::CheckboxFlags("Shadow map", &light.mFlags, LightFlags_Shadowmap);
  if (light.mFlags&LightFlags_Shadowmap)
    ImGui::DragFloat("Shadow Near Plane", &light.mShadowProjection.Near, 0.1f, -1, 1);
}
template<> inline void type_gui_fn(DynamicRenderPass& rp) {
  for (auto& sp : rp.subpasses()) {
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
template<> inline void type_gui_fn(RasterScene::Camera& cam) {
  ImGui::CheckboxFlags("Orthogrpahic", &cam.mProjectionMode, ProjectionMode_Orthographic);
  ImGui::DragFloat("Near Plane", &cam.mNear, 0.01f, -1, 1);
  if (cam.mProjectionMode&ProjectionMode_Orthographic)
    ImGui::DragFloat("Vertical Size", &cam.mOrthographicHeight, .01f);
  else
    ImGui::DragFloat("Vertical FoV", &cam.mVerticalFoV, .01f, 0.00390625, numbers::pi_v<float>);
}
template<> inline void type_gui_fn(PipelineState& pipeline) {
  ImGui::Text("%llu pipelines", pipeline.pipelines().size());
  ImGui::Text("%llu descriptor sets", pipeline.descriptor_sets().size());
}
template<> inline void type_gui_fn(RasterScene& scene) {
  ImGui::DragFloat("Environment Gamma", &scene.background()->push_constant<float>("gEnvironmentGamma"), .01f);
  if (ImGui::BeginCombo("Main Camera", scene.main_camera().node().name().c_str())) {
    scene.node().for_each_descendant<RasterScene::Camera>([&](component_ptr<RasterScene::Camera> c) {
      if (ImGui::Selectable(c.node().name().c_str(), scene.main_camera() == c))
        scene.main_camera(c);
    });
    ImGui::EndCombo();
  }
}

template<typename...Types>
inline void component_interface_fns(RasterScene::PushGeometry& pg, Node& node) {
  auto component_gui_fn = []<typename T>(RasterScene::PushGeometry& pg, Node& node) {
    component_ptr<T> ptr = node.find<T>();
    if (ptr) {
      ImGui::Text(typeid(T).name());
      type_gui_fn(*ptr);
      type_gizmo_fn(pg, *ptr);
    }
  };
  (component_gui_fn.operator()<Types>(pg, node), ...);
}

inline void node_gui_fn(Node& n, Node*& selected) {
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
  if (&n == selected) flags |= ImGuiTreeNodeFlags_Selected;
  if (n.children().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx(n.name().c_str(), flags)) {
    if (ImGui::IsItemClicked())
      selected = &n;
    for (Node& c : n.children()) {
      node_gui_fn(c, selected);
    }
    ImGui::TreePop();
  }
}
#pragma endregion

hlsl::TransformData RasterScene::node_to_world(const Node& node) {
	TransformData transform(float3(0,0,0), 1.f, make_quatf(0,0,0,1));
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<hlsl::TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

RasterScene::RasterScene(Node& node) : mNode(node) {
	auto app = mNode.node_graph().find_components<Application>().front();
	const spirv_module_map& spirv = *mNode.node_graph().find_components<spirv_module_map>().front();

	auto sampler = make_shared<Sampler>(app->window().mInstance.device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	mGeometryPipeline= node.make_child("Geometry Pipeline").make_component<PipelineState>("Opaque Geometry", spirv.at("pbr_vs"), spirv.at("pbr_fs"));
	mGeometryPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eGreaterOrEqual;
	mGeometryPipeline->set_immutable_sampler("gSampler", sampler);
	mGeometryPipeline->set_immutable_sampler("gShadowSampler", make_shared<Sampler>(sampler->mDevice, "gShadowSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		0, true, 2, true, vk::CompareOp::eGreater, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));
	
	mBackgroundPipeline = node.make_child("Background Pipeline").make_component<PipelineState>("Background", spirv.at("basic_skybox_vs"), spirv.at("basic_skybox_fs"));
	mBackgroundPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eGreaterOrEqual;
	mBackgroundPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mBackgroundPipeline->set_immutable_sampler("gSampler", sampler);
	mBackgroundPipeline->specialization_constant("gTextureCount") = 1;
	mBackgroundPipeline->descriptor("gTextures") = sampled_texture_descriptor(Texture::View());

	mBlankTexture = make_shared<Texture>(app->window().mInstance.device(), "blank", vk::Extent3D(2, 2, 1), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled);
	
	mPushGeometry = make_unique<PushGeometry>(sampler->mDevice, *this, node.make_child("Gizmo Pipeline").make_component<PipelineState>("Gizmos", spirv.at("basic_color_texture_vs"), spirv.at("basic_color_texture_fs")));
	mPushGeometry->mPipeline->set_immutable_sampler("gSampler", sampler);
	mPushGeometry->mPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eGreaterOrEqual;

	mShadowNode = node.make_child("ShadowMap DynamicRenderPass").make_component<DynamicRenderPass>();
	auto& shadowPass = *mShadowNode->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mShadowNode, "shadowPass"));
	shadowPass.emplace_attachment("gShadowMap", AttachmentType::eDepthStencil, blend_mode_state(), vk::AttachmentDescription({},
		vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal));
	
	mMainCamera = mNode.make_child("Default Camera").make_component<RasterScene::Camera>(0, 1/1024.f, radians(70.f));
	mMainCamera.node().make_component<TransformData>(float3::Ones(), 1.f, make_quatf(0,0,0,1));
	
	mNode.listen(shadowPass.OnDraw, [&](CommandBuffer& commandBuffer) {
		mNode.for_each_descendant<LightData>([&](auto light) {
			if (light->mFlags&LightFlags_Shadowmap) {
				const auto& extent = commandBuffer.bound_framebuffer()->extent();
				vk::Viewport vp(light->mShadowST[2]*extent.width, light->mShadowST[3]*extent.height, light->mShadowST[0]*extent.width, light->mShadowST[1]*extent.height, 0, 1);
				vk::Rect2D scissor( { (int32_t)vp.x, (int32_t)vp.y }, { (uint32_t)ceilf(vp.width), (uint32_t)ceilf(vp.height) } );
				vp.y += vp.height;
				vp.height = -vp.height;
				commandBuffer->setViewport(0, vp);
				commandBuffer->setScissor(0, scissor);
				mGeometryPipeline->push_constant<TransformData>("gWorldToCamera") = inverse(light->mLightToWorld);
				mGeometryPipeline->push_constant<ProjectionData>("gProjection") = light->mShadowProjection;
				mDrawData->draw(commandBuffer, false);
			}
		});
	});

  mNode.listen(app->render_pass()->mPass.PreProcess, bind_front(&RasterScene::pre_render, this));
  mNode.listen(app->render_pass()->OnDraw, [&](CommandBuffer& commandBuffer) { draw(commandBuffer, mMainCamera); });
  mNode.listen(app->OnUpdate, [=,this](CommandBuffer& commandBuffer, float deltaTime) { mPushGeometry->clear(); }, EventPriority::eFirst + 32);

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
  
  mNode.listen(app->OnUpdate, [=,this](CommandBuffer& commandBuffer, float deltaTime) {
		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();
		
    Profiler::on_gui();
    
    if (ImGui::Begin("Load Asset")) {
      for (const auto&[filepath, name, type] : assets)
        if (ImGui::Button(name.c_str()))
          switch (type) {
            case AssetType::eScene:
              load_gltf(commandBuffer, filepath);
              break;
            case AssetType::eEnvironmentMap: {
              auto[pixels,extent] = Texture::load(commandBuffer.mDevice, filepath);
              Texture::View tex = make_shared<Texture>(commandBuffer.mDevice, name, extent, pixels.format(), 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled);
              commandBuffer.copy_buffer_to_image(pixels, tex);
              tex.texture()->generate_mip_maps(commandBuffer);
              mBackgroundPipeline->push_constant<float>("gEnvironmentGamma") = 2.2f;
              mBackgroundPipeline->descriptor("gTextures") = sampled_texture_descriptor(tex);
              break;
            }
          }
    }
    ImGui::End();
    
    // scenegraph inspector
    if (ImGui::Begin("Scene Graph")) {
      ImGui::Columns(2);
  		static Node* selected = nullptr;
      node_gui_fn(mNode.root(), selected);
      ImGui::NextColumn();
      if (selected) {
        if (ImGui::BeginChild(selected->name().c_str()))
          component_interface_fns<
            Application,
            Instance,
            spirv_module_map,
            RasterScene,
            DynamicRenderPass,
            Gui,
            TransformData,
            LightData,
            RasterScene::Camera,
            RasterScene::MeshInstance,
            PipelineState
          >(push_geometry(), *selected);
        ImGui::EndChild();
      }
    }
    ImGui::End();

    // camera controls
    Window& window = commandBuffer.mDevice.mInstance.window();
    auto cameraTransform = mMainCamera.node().find_in_ancestor<TransformData>();
    float fwd = (mMainCamera->mNear < 0) ? -1 : 1;
    if (!ImGui::GetIO().WantCaptureMouse) {
      if (window.pressed(KeyCode::eMouse2)) {
				static float2 euler = float2::Zero();
				euler.y() += window.cursor_delta().x() * .0025f;
        euler.x() = clamp(euler.x() + window.cursor_delta().y() * .0025f, -numbers::pi_v<float>/2, numbers::pi_v<float>/2);
        quatf rx = angle_axis(euler.x(), float3(1,0,0));
        quatf ry = angle_axis(euler.y(), float3(0,1,0));
        cameraTransform->Rotation = qmul(ry, rx);
      }
    }
    if (!ImGui::GetIO().WantCaptureKeyboard) {
      float3 mv = float3(0,0,0);
      if (window.pressed(KeyCode::eKeyD)) mv += float3( 1,0,0);
      if (window.pressed(KeyCode::eKeyA)) mv += float3(-1,0,0);
      if (window.pressed(KeyCode::eKeyW)) mv += float3(0,0, fwd);
      if (window.pressed(KeyCode::eKeyS)) mv += float3(0,0,-fwd);
      cameraTransform->Translation += rotate_vector(cameraTransform->Rotation, mv*deltaTime);
    }
  });
}

void RasterScene::load_gltf(CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("pbrRenderer::load_gltf", commandBuffer);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	string err, warn;
	if (
		(filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
		(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) )
		throw runtime_error(filename.string() + ": " + err);
	if (!warn.empty()) fprintf_color(ConsoleColor::eYellow, stderr, "%s: %s\n", filename.string().c_str(), warn.c_str());
	
	Device& device = commandBuffer.mDevice;

	vector<shared_ptr<Buffer>> buffers(model.buffers.size());
	vector<shared_ptr<Texture>> images(model.images.size());
	buffer_vector<MaterialData> materials(commandBuffer.mDevice, model.materials.size());
	vector<vector<MeshInstance>> meshes(model.meshes.size());

	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<byte> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst);
		Buffer::View<byte> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", dst.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(tmp.data(), buffer.data.data(), tmp.size());
		commandBuffer.copy_buffer(tmp, dst);
		return dst.buffer();
	});
	ranges::transform(model.images, images.begin(), [&](const tinygltf::Image& image) {
		Buffer::View<byte> pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(pixels.data(), image.image.data(), pixels.size_bytes());
		
		static const unordered_map<int, std::array<vk::Format,4>> formatMap {
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, 	{ vk::Format::eR8Unorm, vk::Format::eR8G8Unorm, vk::Format::eR8G8B8Unorm, vk::Format::eR8G8B8A8Unorm } },
			{ TINYGLTF_COMPONENT_TYPE_BYTE, 					{ vk::Format::eR8Snorm, vk::Format::eR8G8Snorm, vk::Format::eR8G8B8Snorm, vk::Format::eR8G8B8A8Snorm } },
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, { vk::Format::eR16Unorm, vk::Format::eR16G16Unorm, vk::Format::eR16G16B16Unorm, vk::Format::eR16G16B16A16Unorm } },
			{ TINYGLTF_COMPONENT_TYPE_SHORT, 					{ vk::Format::eR16Snorm, vk::Format::eR16G16Snorm, vk::Format::eR16G16B16Snorm, vk::Format::eR16G16B16A16Snorm } },
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, 	{ vk::Format::eR32Uint, vk::Format::eR32G32Uint, vk::Format::eR32G32B32Uint, vk::Format::eR32G32B32A32Uint } },
			{ TINYGLTF_COMPONENT_TYPE_INT, 						{ vk::Format::eR32Sint, vk::Format::eR32G32Sint, vk::Format::eR32G32B32Sint, vk::Format::eR32G32B32A32Sint } },
			{ TINYGLTF_COMPONENT_TYPE_FLOAT, 					{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat } },
			{ TINYGLTF_COMPONENT_TYPE_DOUBLE, 				{ vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat, vk::Format::eR64G64B64A64Sfloat } }
		};
				
		vk::Format fmt = formatMap.at(image.pixel_type).at(image.component - 1);
		
		auto tex = make_shared<Texture>(device, image.name, vk::Extent3D(image.width, image.height, 1), fmt, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.copy_buffer_to_image(pixels, tex);
		tex->generate_mip_maps(commandBuffer);
		return tex;
	});
	ranges::transform(model.materials, materials.begin(), [](const tinygltf::Material& material) {
		MaterialData m;
		m.mEmission = Map<const Array3d>(material.emissiveFactor.data()).cast<float>();
		m.mBaseColor = Map<const Array4d>(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
		m.mMetallic = (float)material.pbrMetallicRoughness.metallicFactor;
		m.mRoughness = (float)material.pbrMetallicRoughness.roughnessFactor;
		m.mNormalScale = (float)material.normalTexture.scale;
		m.mOcclusionScale = (float)material.occlusionTexture.strength;
		TextureIndices inds;
		inds.mBaseColor = material.pbrMetallicRoughness.baseColorTexture.index;
		inds.mNormal = material.normalTexture.index;
		inds.mEmission = material.emissiveTexture.index;
		inds.mMetallic = inds.mRoughness = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
		inds.mOcclusion = material.occlusionTexture.index;
		inds.mMetallicChannel = 0;
		inds.mRoughnessChannel = 1;
		inds.mOcclusionChannel = 0;
		m.mTextureIndices = pack_texture_indices(inds);
		return m;
	});
	for (uint32_t i = 0; i < model.meshes.size(); i++) {
		meshes[i].resize(model.meshes[i].primitives.size());
		for (uint32_t j = 0; j < model.meshes[i].primitives.size(); j++) {
			const tinygltf::Primitive& prim = model.meshes[i].primitives[j];

			shared_ptr<Geometry> geometry = make_shared<Geometry>();
			
			vk::PrimitiveTopology topology;
			switch (prim.mode) {
				case TINYGLTF_MODE_POINTS: 					topology = vk::PrimitiveTopology::ePointList; break;
				case TINYGLTF_MODE_LINE: 						topology = vk::PrimitiveTopology::eLineList; break;
				case TINYGLTF_MODE_LINE_LOOP: 			topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_LINE_STRIP: 			topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_TRIANGLES: 			topology = vk::PrimitiveTopology::eTriangleList; break;
				case TINYGLTF_MODE_TRIANGLE_STRIP: 	topology = vk::PrimitiveTopology::eTriangleStrip; break;
				case TINYGLTF_MODE_TRIANGLE_FAN: 		topology = vk::PrimitiveTopology::eTriangleFan; break;
			}
			
			for (const auto&[attribName,attribIndex] : prim.attributes) {
				const tinygltf::Accessor& accessor = model.accessors[attribIndex];

				static const unordered_map<int, unordered_map<int, vk::Format>> formatMap {
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_FLOAT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sfloat },
					} },
					{ TINYGLTF_COMPONENT_TYPE_DOUBLE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR64Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR64G64Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR64G64B64Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR64G64B64A64Sfloat },
					} }
				};
				vk::Format attributeFormat = formatMap.at(accessor.componentType).at(accessor.type);

				Geometry::AttributeType attributeType;
				uint32_t typeIndex = 0;
				// parse typename & typeindex
				{
					string typeName;
					typeName.resize(attribName.size());
					ranges::transform(attribName, typeName.begin(), [&](char c) { return tolower(c); });
					size_t c = typeName.find_first_of("0123456789");
					if (c != string::npos) {
						typeIndex = stoi(typeName.substr(c));
						typeName = typeName.substr(0, c);
					}
					if (typeName.back() == '_') typeName.pop_back();
					static const unordered_map<string, Geometry::AttributeType> semanticMap {
						{ "position", 	Geometry::AttributeType::ePosition },
						{ "normal", 		Geometry::AttributeType::eNormal },
						{ "tangent", 		Geometry::AttributeType::eTangent },
						{ "bitangent", 	Geometry::AttributeType::eBinormal },
						{ "texcoord", 	Geometry::AttributeType::eTexcoord },
						{ "color", 			Geometry::AttributeType::eColor },
						{ "psize", 			Geometry::AttributeType::ePointSize },
						{ "pointsize", 	Geometry::AttributeType::ePointSize },
						{ "joints",     Geometry::AttributeType::eBlendIndex },
						{ "weights",    Geometry::AttributeType::eBlendWeight }
					};
					attributeType = semanticMap.at(typeName);
				}
				
				auto& attribs = (*geometry)[attributeType];
				if (attribs.size() <= typeIndex) attribs.resize(typeIndex+1);
				const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
				attribs[typeIndex] = {
					Geometry::AttributeDescription(accessor.ByteStride(bv), attributeFormat, (uint32_t)accessor.byteOffset, vk::VertexInputRate::eVertex),
					Buffer::StrideView(buffers[bv.buffer], accessor.ByteStride(bv), bv.byteOffset, bv.byteLength) };
			}
		
			size_t indexStride = 0;
			vk::IndexType indexType;
			const auto& indicesAccessor = model.accessors[prim.indices];
			switch (indicesAccessor.componentType) {
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			case TINYGLTF_COMPONENT_TYPE_BYTE:
				indexStride = sizeof(uint8_t);
				indexType = vk::IndexType::eUint8EXT;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			case TINYGLTF_COMPONENT_TYPE_SHORT:
				indexStride = sizeof(uint16_t);
				indexType = vk::IndexType::eUint16;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			case TINYGLTF_COMPONENT_TYPE_INT:
				indexStride = sizeof(uint32_t);
				indexType = vk::IndexType::eUint32;
				break;
			}
			
			Mesh mesh(geometry, Buffer::StrideView(buffers[model.bufferViews[indicesAccessor.bufferView].buffer], indexStride, model.bufferViews[indicesAccessor.bufferView].byteOffset, model.bufferViews[indicesAccessor.bufferView].byteLength), topology);
			meshes[i][j] = MeshInstance(mesh, indicesAccessor.count, 0, (uint32_t)(indicesAccessor.byteOffset/indexStride), prim.material, 0);
		}
	}

	mGeometryPipeline->specialization_constant("gTextureCount") = max(1u, (uint32_t)images.size());
	for (uint32_t i = 0; i < images.size(); i++)
		mGeometryPipeline->descriptor("gTextures", i) = sampled_texture_descriptor(images[i]);
	if (images.empty())
		mGeometryPipeline->descriptor("gTextures") = sampled_texture_descriptor(mBlankTexture);
	mGeometryPipeline->descriptor("gMaterials") = commandBuffer.copy_buffer(materials, vk::BufferUsageFlagBits::eStorageBuffer);

	vector<Node*> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		const auto& node = model.nodes[n];
		Node& dst = mNode.make_child(node.name);
		nodes[n] = &dst;
		
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			auto transform = dst.make_component<TransformData>(float3::Zero(), 1.f, make_quatf(0,0,0,1));
			if (!node.translation.empty()) transform->Translation = Map<const Array3d>(node.translation.data()).cast<float>();
			if (!node.rotation.empty()) 	 transform->Rotation = make_quatf((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			if (!node.scale.empty()) 			 transform->Scale = (float)Map<const Vector3d>(node.scale.data()).norm();
		}

		if (node.mesh < model.meshes.size())
			for (uint32_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++)
				dst.make_child(model.meshes[node.mesh].name).make_component<MeshInstance>(meshes[node.mesh][i]);

		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			auto light = dst.make_child(l.name).make_component<LightData>();
			light->mEmission = (Map<const Array3d>(l.color.data()) * l.intensity).cast<float>();
			if (l.type == "directional") {
				light->mType = LightType_Distant;
				light->mShadowProjection = make_orthographic(float2(16, 16), float2::Zero(), -1/1024.f);
			} else if (l.type == "point") {
				light->mType = LightType_Point;
				light->mShadowProjection = make_perspective(numbers::pi_v<float>/2, 1, float2::Zero(), -1/1024.f);
			} else if (l.type == "spot") {
				light->mType = LightType_Spot;
				double co = cos(l.spot.outerConeAngle);
				light->mSpotAngleScale = (float)(1/(cos(l.spot.innerConeAngle) - co));
				light->mSpotAngleOffset = -(float)(co * light->mSpotAngleScale);
				light->mShadowProjection = make_perspective((float)l.spot.outerConeAngle, 1, float2::Zero(), -1/1024.f);
			}
			light->mFlags = LightFlags_Shadowmap;
			light->mShadowBias = .000001f;
			light->mShadowST = Vector4f(1,1,0,0);
		}

		if (node.camera != -1) {
			const tinygltf::Camera& cam = model.cameras[node.camera];
			if (cam.type == "perspective")
				dst.make_child(cam.name).make_component<Camera>(0, -(float)cam.perspective.znear, (float)cam.perspective.yfov);
			else if (cam.type == "orthographic")
				dst.make_child(cam.name).make_component<Camera>(ProjectionMode_Orthographic, -(float)cam.orthographic.znear, (float)cam.orthographic.ymag);
		}
	}

	for (size_t i = 0; i < model.nodes.size(); i++)
		for (int c : model.nodes[i].children)
			nodes[c]->set_parent(*nodes[i]);

	cout << "Loaded " << filename << endl;
}

void RasterScene::pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
	ProfilerRegion s("RasterScene::pre_render", commandBuffer);

	if (!mDrawData) {
		mDrawData = make_unique<DrawData>(*this);
		
		Buffer::View<uint32_t> staging = make_shared<Buffer>(commandBuffer.mDevice, "blank upload", 16, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_COPY);
		memset(staging.data(), ~(0u), staging.size_bytes());
		commandBuffer.copy_buffer_to_image(staging, mBlankTexture);
	}
	mDrawData->clear();

	buffer_vector<TransformData> transforms(commandBuffer.mDevice);
	mNode.for_each_descendant<MeshInstance>([&](const component_ptr<MeshInstance>& instance) {
		instance->mInstanceIndex = (uint32_t)transforms.size();
		transforms.emplace_back(node_to_world(instance.node()));
		mDrawData->add_instance(instance);
	});
	if (!transforms.empty()) {
		buffer_vector<LightData> lights(commandBuffer.mDevice);
		mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
			light->mLightToWorld = node_to_world(light.node());
			lights.emplace_back(*light);
		});

		Texture::View shadowMap;
		Descriptor& descriptor = mGeometryPipeline->descriptor("gShadowMap");
		if (descriptor.index() == 0) {
			Texture::View tex = get<Texture::View>(descriptor);
			if (tex) shadowMap = tex;
		}
		if (!shadowMap) {
			ProfilerRegion s("make gShadowMap");
			shadowMap = make_shared<Texture>(commandBuffer.mDevice, "gShadowMap", vk::Extent3D(2048,2048,1), vk::Format::eD32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
			mGeometryPipeline->descriptor("gShadowMap") = sampled_texture_descriptor(shadowMap);
		}
		mGeometryPipeline->descriptor("gTransforms") = commandBuffer.copy_buffer(transforms, vk::BufferUsageFlagBits::eStorageBuffer);
		mGeometryPipeline->descriptor("gLights") = commandBuffer.copy_buffer(lights, vk::BufferUsageFlagBits::eStorageBuffer);
		mGeometryPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();
		mShadowNode->render(commandBuffer, { { "gShadowMap", { shadowMap, vk::ClearValue({0.f, 0}) } } });
	}

	mGeometryPipeline->transition_images(commandBuffer);
	mBackgroundPipeline->transition_images(commandBuffer);

	mPushGeometry->pre_render(commandBuffer);
}
void RasterScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading) const {
	const auto& framebuffer = commandBuffer.bound_framebuffer();
	auto worldToCamera = inverse(node_to_world(camera.node()));
	auto projection = (camera->mProjectionMode & ProjectionMode_Orthographic) ?
		make_orthographic(float2(camera->mOrthographicHeight, (float)framebuffer->extent().height/(float)framebuffer->extent().width), float2::Zero(), camera->mNear) :
		make_perspective(camera->mVerticalFoV, (float)framebuffer->extent().height/(float)framebuffer->extent().width, float2::Zero(), camera->mNear);

	mGeometryPipeline->push_constant<TransformData>("gWorldToCamera") = worldToCamera;
	mGeometryPipeline->push_constant<ProjectionData>("gProjection") = projection;
	mDrawData->draw(commandBuffer, doShading);

	if (doShading) {
		mPushGeometry->draw(commandBuffer, worldToCamera, projection);
		if (get<Texture::View>(mBackgroundPipeline->descriptor("gTextures"))) {
			mBackgroundPipeline->push_constant<TransformData>("gWorldToCamera") = worldToCamera;
			mBackgroundPipeline->push_constant<ProjectionData>("gProjection") = projection;
			commandBuffer.bind_pipeline(mBackgroundPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), GeometryStateDescription(vk::PrimitiveTopology::eTriangleList)));
			mBackgroundPipeline->bind_descriptor_sets(commandBuffer);
			mBackgroundPipeline->push_constants(commandBuffer);
			commandBuffer->draw(6, 1, 0, 0);
		}
	}
}

void RasterScene::DrawData::clear() {
	for (auto&[geometry, instances] : mMeshInstances) instances.clear();
}
void RasterScene::DrawData::draw(CommandBuffer& commandBuffer, bool doShading) const {
	ProfilerRegion ps("RasterScene::draw", commandBuffer);
	for (const auto&[geometryHash, instances] : mMeshInstances) {
		commandBuffer.bind_pipeline(
			mGeometryPipeline->get_pipeline(
				commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(),
				instances.begin()->second.front()->mMesh.description(*mGeometryPipeline->stage(vk::ShaderStageFlagBits::eVertex)),
				doShading ? vk::ShaderStageFlagBits::eAll : vk::ShaderStageFlagBits::eVertex ) );
		mGeometryPipeline->bind_descriptor_sets(commandBuffer);
		mGeometryPipeline->push_constants(commandBuffer);
		for (const auto&[materialIdx, instanceVec] : instances) {
			commandBuffer.push_constant("gMaterialIndex", materialIdx);
			for (const auto& instance : instanceVec) {
				instance->mMesh.bind(commandBuffer);
				commandBuffer->drawIndexed(instance->mIndexCount, 1, instance->mFirstIndex, instance->mFirstVertex, instance->mInstanceIndex);
			}
		}
	}
}

#define SPHERE_RESOLUTION 32
const array<uint16_t,6> gQuadIndices { 0,1,2, 0,2,3 };

void RasterScene::PushGeometry::clear() {
	mInstances.clear();
	mVertices.reset();
	mIndices.reset();
	
	{
		// TODO: replace this with circle
		uint32_t firstIndex = (uint32_t)mIndices.size();
		mVertices.resize(mVertices.size() + SPHERE_RESOLUTION*SPHERE_RESOLUTION);
		mIndices.resize(mIndices.size() + (SPHERE_RESOLUTION-1)*(SPHERE_RESOLUTION-1)*gQuadIndices.size());
		for (uint32_t i = 0; i < SPHERE_RESOLUTION; i++) {
			const float theta = float(i)/float(SPHERE_RESOLUTION-1);
			const float sinTheta = sinf(theta);
			for (uint32_t j = 0; j < SPHERE_RESOLUTION; j++) {
				const float phi = float(j)/float(SPHERE_RESOLUTION-1);
				mVertices[j + i*SPHERE_RESOLUTION].mColor = float4::Ones();
				mVertices[j + i*SPHERE_RESOLUTION].mPosition = float3(cosf(phi)*sinTheta, cosf(theta), sinf(phi)*sinTheta);
				mVertices[j + i*SPHERE_RESOLUTION].mTexcoord = float2(phi, theta);
				if (i < SPHERE_RESOLUTION-1 && j < SPHERE_RESOLUTION-1)
					ranges::transform(gQuadIndices, mIndices.begin() + firstIndex + j + i*(SPHERE_RESOLUTION-1), [](auto i) { return i + j + i*(SPHERE_RESOLUTION-1); });
			}
		}
	}
	{
		uint32_t firstIndex = (uint32_t)mIndices.size();
		mIndices.resize(mIndices.size() + gQuadIndices.size());
		ranges::copy(gQuadIndices, mIndices.begin());
	}
}
void RasterScene::PushGeometry::pre_render(CommandBuffer& commandBuffer) {
	if (mInstances.empty()) return;

	mPipeline->specialization_constant("gTextureCount") = (uint32_t)mTextures.size();
	for (const auto&[tex, idx] : mTextures)
		mPipeline->descriptor("gTextures", idx) = sampled_texture_descriptor(tex);
	mPipeline->transition_images(commandBuffer);

	if (!mDrawData.first) {
		mDrawData.first = make_shared<Geometry>(Geometry::AttributeType::ePosition, Geometry::AttributeType::eTexcoord, Geometry::AttributeType::eColor);
		(*mDrawData.first)[Geometry::AttributeType::ePosition][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32B32Sfloat,    offsetof(vertex_t, mPosition), vk::VertexInputRate::eVertex);
		(*mDrawData.first)[Geometry::AttributeType::eTexcoord][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32Sfloat,       offsetof(vertex_t, mTexcoord), vk::VertexInputRate::eVertex);
		(*mDrawData.first)[Geometry::AttributeType::eColor   ][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32B32A32Sfloat, offsetof(vertex_t, mColor   ), vk::VertexInputRate::eVertex);
	}
	(*mDrawData.first)[Geometry::AttributeType::ePosition][0].second = mVertices.buffer_view();
	(*mDrawData.first)[Geometry::AttributeType::eTexcoord][0].second = mVertices.buffer_view();
	(*mDrawData.first)[Geometry::AttributeType::eColor   ][0].second = mVertices.buffer_view();
	mDrawData.second = mIndices.buffer_view();
}
void RasterScene::PushGeometry::draw(CommandBuffer& commandBuffer, const TransformData& worldToCamera, const ProjectionData& projection) const {
	mPipeline->push_constant<ProjectionData>("gProjection") = projection;
	for (const Instance& i : mInstances) {
		if (i.mTransform.Rotation.xyz.isZero() && i.mTransform.Rotation.w == 0)
			mPipeline->push_constant<TransformData>("gWorldToCamera") = TransformData(transform_point(worldToCamera, i.mTransform.Translation), worldToCamera.Scale*i.mTransform.Scale, make_quatf(0,0,0,1));
		else
			mPipeline->push_constant<TransformData>("gWorldToCamera") = tmul(worldToCamera, i.mTransform);
		mPipeline->push_constant<float4>("gColor") = i.mColor;
		mPipeline->push_constant<float4>("gTextureST") = i.mTextureST;
		mPipeline->push_constant<uint32_t>("gTextureIndex") = i.mTextureIndex;
		
		Mesh mesh(mDrawData.first, mDrawData.second, i.mTopology);
		commandBuffer.bind_pipeline(mPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), mesh.description(*mPipeline->stage(vk::ShaderStageFlagBits::eVertex))));
		mPipeline->bind_descriptor_sets(commandBuffer);
		mPipeline->push_constants(commandBuffer);
		mesh.bind(commandBuffer);
		commandBuffer->drawIndexed(i.mIndexCount, 1, i.mFirstIndex, i.mFirstVertex, 0);
	}
}

uint32_t RasterScene::PushGeometry::texture_index(const Texture::View& texture) {
	uint32_t tidx = ~0;
	if (auto it = mTextures.find(texture); it != mTextures.end())
		tidx = it->second;
	else {
		tidx = (uint32_t)mTextures.size();
		mTextures.emplace(texture, tidx);
	}
	return tidx;
}

void RasterScene::PushGeometry::sphere(const float3& position, float size, const float4& color, const Texture::View& texture, const float4& textureST) {
	mInstances.emplace_back((SPHERE_RESOLUTION-1)*(SPHERE_RESOLUTION-1)*6, 0, 0, vk::PrimitiveTopology::eTriangleList, TransformData(position, size, make_quatf(0,0,0,1)), color, textureST, texture_index(texture ? texture : mBlankTexture));
}
void RasterScene::PushGeometry::quad(const float3& position, float rotation, const float2& size, const float4& color, const Texture::View& texture, const float4& textureST) {
	mInstances.emplace_back(6, (uint32_t)mVertices.size(), (SPHERE_RESOLUTION-1)*(SPHERE_RESOLUTION-1)*6, vk::PrimitiveTopology::eTriangleList, TransformData(position, 1.f, make_quatf(0,0,0,0)), float4::Ones(), textureST, texture_index(texture ? texture : mBlankTexture));
	float2 sz = size/2;
	mVertices.emplace_back(float3( sz[0],-sz[1],0), color, float2(1,0));
	mVertices.emplace_back(float3( sz[0], sz[1],0), color, float2(1,1));
	mVertices.emplace_back(float3(-sz[0], sz[1],0), color, float2(0,1));
	mVertices.emplace_back(float3(-sz[0],-sz[1],0), color, float2(0,0));
}