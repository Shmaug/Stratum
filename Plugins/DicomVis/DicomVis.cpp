#include <Core/EnginePlugin.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Renderers/MeshRenderer.hpp>
#include <Util/Profiler.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <ThirdParty/tiny_gltf.h>

#include "RenderVolume.hpp"

using namespace std;

vk::Format gltf2vk(int componentType, int componentCount) {
	switch (componentType) {
		case TINYGLTF_COMPONENT_TYPE_BYTE:
			return (vector<vk::Format> { vk::Format::eR8Snorm, vk::Format::eR8G8Snorm, vk::Format::eR8G8B8Snorm, vk::Format::eR8G8B8A8Snorm })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return (vector<vk::Format> { vk::Format::eR8Unorm, vk::Format::eR8G8Unorm, vk::Format::eR8G8B8Unorm, vk::Format::eR8G8B8A8Unorm })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_SHORT:
			return (vector<vk::Format> { vk::Format::eR16Snorm, vk::Format::eR16G16Snorm, vk::Format::eR16G16B16Snorm, vk::Format::eR16G16B16A16Snorm })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			return (vector<vk::Format> { vk::Format::eR16Unorm, vk::Format::eR16G16Unorm, vk::Format::eR16G16B16Unorm, vk::Format::eR16G16B16A16Unorm })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_INT:
			return (vector<vk::Format> { vk::Format::eR32Sint, vk::Format::eR32G32Sint, vk::Format::eR32G32B32Sint, vk::Format::eR32G32B32A32Sint })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			return (vector<vk::Format> { vk::Format::eR32Uint, vk::Format::eR32G32Uint, vk::Format::eR32G32B32Uint, vk::Format::eR32G32B32A32Uint })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			return (vector<vk::Format> { vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat })[componentCount-1];
		case TINYGLTF_COMPONENT_TYPE_DOUBLE:
			return (vector<vk::Format> { vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat, vk::Format::eR64G64B64A64Sfloat })[componentCount-1];
	}
	return vk::Format::eUndefined;
}

class DicomVis : public EnginePlugin {
private:
	Scene* mScene = nullptr;
	MouseKeyboardInput* mKeyboardInput = nullptr;
	Camera* mMainCamera = nullptr;
	RenderVolume* mVolume = nullptr;

	float mZoom = 0;
	bool mShowPerformance = false;

	std::set<fs::path> mStackFolders;

	void ScanFolders() {
		fs::path dataPath;
		vector<fs::path> defaultLocations { "/Data", "/data", "~/Data", "~/data", "C:/Data", "D:/Data", "E:/Data", "F:/Data" "G:/Data", };
		for (auto it = mScene->Instance()->ArgsBegin(); it != mScene->Instance()->ArgsEnd(); it++)
			if (*it == "--datapath" && ++it != mScene->Instance()->ArgsEnd())
					dataPath = *it;
		auto it = defaultLocations.begin();
		while (!fs::exists(dataPath) && it != defaultLocations.end()) dataPath = *it++;
		if (!fs::exists(dataPath)) {
			fprintf_color(ConsoleColorBits::eRed, stderr, "DicomVis: Could not locate image data path. Please specify with --datapath <path>\n");
			return;
		}

		for (const auto& p : fs::recursive_directory_iterator(dataPath)) {
			if (!p.is_directory() || p.path().stem() == "_mask" || ImageLoader::FolderStackType(p.path()) == ImageStackType::eNone) continue;
			mStackFolders.insert(p.path());
		}
	}
	void LoadScene() {
		fs::path filename;
		for (auto it = mScene->Instance()->ArgsBegin(); it != mScene->Instance()->ArgsEnd(); it++)
			if (*it == "--environment" && ++it != mScene->Instance()->ArgsEnd())
				filename = *it;

		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;
		if (
			(filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
			(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) ) {
			fprintf_color(ConsoleColorBits::eRed, stderr, "%s: %s\n", filename.string().c_str(), err.c_str());
			return;
		}
		if (!warn.empty()) fprintf_color(ConsoleColorBits::eYellow, stderr, "%s: %s\n", filename.string().c_str(), warn.c_str());
		
		Device* device = mScene->Instance()->Device();
		vector<stm_ptr<Buffer>> buffers;
		vector<stm_ptr<Texture>> images;
		vector<vector<stm_ptr<Mesh>>> meshes;
		vector<stm_ptr<Material>> materials;

		for (const auto& b : model.buffers)
			buffers.push_back(new Buffer(b.name, mScene->Instance()->Device(), b.data.data(), b.data.size(), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer));

		for (const auto& i : model.images)
			images.push_back(new Texture(i.name, device, (void*)i.image.data(), i.image.size(), vk::Extent3D(i.width, i.height, 1), gltf2vk(i.pixel_type, i.component), 1));

		for (const auto& m : model.materials) {
			stm_ptr<Material> mat = new Material(m.name, mScene->Instance()->Device()->AssetManager()->Load<Pipeline>("Shaders/pbr.stmb", "pbr"));
			materials.push_back(mat);
		}

		unordered_map<string, VertexAttributeType> semanticMap {
			{ "position", VertexAttributeType::ePosition },
			{ "normal", VertexAttributeType::eNormal },
			{ "tangent", VertexAttributeType::eTangent },
			{ "bitangent", VertexAttributeType::eBitangent },
			{ "texcoord", VertexAttributeType::eTexcoord },
			{ "color", VertexAttributeType::eColor },
			{ "psize", VertexAttributeType::ePointSize },
			{ "pointsize", VertexAttributeType::ePointSize }
		};

		for (const auto& m : model.meshes) {
			meshes.push_back({});
			for (const auto& prim : m.primitives) {

				vk::PrimitiveTopology topo = vk::PrimitiveTopology::eTriangleList;
				switch (prim.mode) {
					case TINYGLTF_MODE_POINTS: topo = vk::PrimitiveTopology::ePointList; break;
					case TINYGLTF_MODE_LINE: topo = vk::PrimitiveTopology::eLineList; break;
					case TINYGLTF_MODE_LINE_LOOP: topo = vk::PrimitiveTopology::eLineStrip; break;
					case TINYGLTF_MODE_LINE_STRIP: topo = vk::PrimitiveTopology::eLineStrip; break;
					case TINYGLTF_MODE_TRIANGLES: topo = vk::PrimitiveTopology::eTriangleList; break;
					case TINYGLTF_MODE_TRIANGLE_STRIP: topo = vk::PrimitiveTopology::eTriangleStrip; break;
					case TINYGLTF_MODE_TRIANGLE_FAN: topo = vk::PrimitiveTopology::eTriangleFan; break;
				}
				
				stm_ptr<Mesh> mesh(new Mesh(m.name, topo));
				const auto& indices = model.accessors[prim.indices];
				const auto& indexBufferView = model.bufferViews[indices.bufferView];
				mesh->SetIndexBuffer(BufferView(buffers[indexBufferView.buffer], indexBufferView.byteOffset), indices.ByteStride(indexBufferView) == sizeof(uint16_t) ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
				
				uint32_t vertexCount = 0;
				for (const auto& attrib : prim.attributes) {
					string typeName = attrib.first;
					const auto& accessor = model.accessors[attrib.second];

					// parse typename
					transform(typeName.begin(), typeName.end(), typeName.begin(), [](char c) { return std::tolower(c); });
					uint32_t typeIdx = 0;
					size_t stridx = typeName.find_first_of("0123456789");
					if (stridx != string::npos) {
						typeIdx = atoi(typeName.c_str() + stridx);
						typeName = typeName.substr(0, stridx);
					}
					if (typeName.back() == '_') typeName = typeName.substr(0, typeName.length() - 1);

					VertexAttributeType type = semanticMap.count(typeName) ? semanticMap.at(typeName) : VertexAttributeType::eOther;

					uint32_t channelc = 0;
					switch (accessor.type) {
						case TINYGLTF_TYPE_SCALAR: channelc = 1; break;
						case TINYGLTF_TYPE_VEC2: channelc = 2; break;
						case TINYGLTF_TYPE_VEC3: channelc = 3; break;
						case TINYGLTF_TYPE_VEC4: channelc = 4; break;
					}
					
					const auto& bufferView = model.bufferViews[accessor.bufferView];
					mesh->SetAttribute(type, typeIdx, BufferView(buffers[bufferView.buffer], bufferView.byteOffset), (uint32_t)accessor.byteOffset, (uint32_t)bufferView.byteStride);

					vertexCount = (uint32_t)accessor.count;
				}

				mesh->AddSubmesh(Mesh::Submesh(vertexCount, 0, (uint32_t)indices.count, 0, nullptr));
				meshes.back().push_back(mesh);
			}
		}

		for (const auto& s : model.scenes)
			for (auto& n : s.nodes) {
				if (n < 0 || n >= model.nodes.size()) continue;
				const auto& node = model.nodes[n];
				if (node.mesh < 0 || node.mesh >= model.meshes.size()) continue;
				const auto& nodeMesh = model.meshes[node.mesh];
				uint32_t primIndex = 0;
				for (const auto& prim : nodeMesh.primitives) {
					MeshRenderer* renderer = mScene->CreateObject<MeshRenderer>(nodeMesh.name);
					renderer->Mesh(meshes[node.mesh][primIndex]);
					renderer->Material(materials[prim.material]);
					if (node.translation.size() == 3)
						renderer->LocalPosition((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
					if (node.rotation.size() == 4)
						renderer->LocalRotation(quaternion((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]));
					if (node.scale.size() == 3)
						renderer->LocalScale((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
					primIndex++;
				}
			}
	
		buffers.clear();
	}

protected:
	PLUGIN_EXPORT bool OnSceneInit(Scene* scene) override {
		mScene = scene;
		mKeyboardInput = mScene->Instance()->InputManager()->GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;
		
		mMainCamera = mScene->CreateObject<Camera>("Camera", set<RenderTargetIdentifier> { "stm_main_render", "stm_main_resolve" "stm_main_depth" });
		mMainCamera->Near(.00625f);
		mMainCamera->Far(1024.f);
		mMainCamera->FieldOfView(radians(65.f));
		mMainCamera->LocalPosition(0, 1.6f, -mZoom);

		mScene->AmbientLight(.5f);

		auto[extent, usageFlags] = mScene->GetAttachmentInfo("stm_main_resolve");
		mScene->SetAttachmentInfo("stm_main_resolve", extent, usageFlags | vk::ImageUsageFlagBits::eStorage);

		ScanFolders();
		//LoadScene();

		return true;
	}
	PLUGIN_EXPORT void OnUpdate(stm_ptr<CommandBuffer> commandBuffer) override {
		if (mKeyboardInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->ScrollDelta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->ScrollDelta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);
			}
			if (mVolume && mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x - mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f) {
					mVolume->LocalRotation(quaternion(length(axis) * .003f, -normalize(axis)) * mVolume->LocalRotation());
				}
			}
		}
	}
	PLUGIN_EXPORT void OnLateUpdate(stm_ptr<CommandBuffer> commandBuffer) override { if (mVolume) mVolume->UpdateBake(commandBuffer); }
	
	PLUGIN_EXPORT void OnGui(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, GuiContext* gui) override {		
		bool worldSpace = camera->StereoMode() != StereoMode::eNone;

		// Draw performance overlay
		#ifdef PROFILER_ENABLE
		if (mShowPerformance && !worldSpace) Profiler::DrawGui(gui, (uint32_t)mScene->FPS());
		#endif

		if (worldSpace)
			gui->BeginWorldLayout(GuiContext::LayoutAxis::eVertical, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 300, 850));
		else
			gui->BeginScreenLayout(GuiContext::LayoutAxis::eVertical, fRect2D(10, (float)mScene->Instance()->Window()->SwapchainExtent().height - 450 - 10, 300, 450));


		gui->LayoutTitle("Load Dataset");
		gui->LayoutSeparator();
		float prev = gui->mLayoutTheme.mControlSize;
		gui->mLayoutTheme.mControlSize = 24;
		gui->BeginScrollSubLayout(175, mStackFolders.size() * (gui->mLayoutTheme.mControlSize + 2*gui->mLayoutTheme.mControlPadding));
		
		for (const auto& folder : mStackFolders)
			if (gui->LayoutTextButton(folder.stem().string(), TextAnchor::eMin)) {
				commandBuffer->Device()->Flush();
				safe_delete(mVolume);
				try {
					mVolume = mScene->CreateObject<RenderVolume>("Dicom Volume", commandBuffer->Device(), folder);
					mVolume->LocalPosition(mMainCamera->WorldPosition() + mMainCamera->WorldRotation() * float3(0,0,0.5f));
				} catch (exception& e) {
					fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to load volume: %s\n", e.what());
				}
			}

		gui->mLayoutTheme.mControlSize = prev;
		gui->EndLayout();

		if (mVolume) mVolume->DrawGui(commandBuffer, camera, gui);

		gui->EndLayout();
	}

	PLUGIN_EXPORT void OnPostProcess(stm_ptr<CommandBuffer> commandBuffer, Framebuffer* framebuffer, const set<Camera*>& cameras) override {
		if (!mVolume) return;
		for (Camera* camera : cameras)
			mVolume->Draw(commandBuffer, framebuffer, camera);
	}
};

ENGINE_PLUGIN(DicomVis)