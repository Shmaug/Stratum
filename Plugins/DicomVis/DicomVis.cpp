#include <Core/EnginePlugin.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Renderers/MeshRenderer.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "RenderVolume.hpp"


namespace dcmvs {

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
	
	vector<unique_ptr<Object>> mSceneObjects;
	unique_ptr<Camera> mMainCamera;
	unique_ptr<RenderVolume> mVolume;

	float mZoom = 0;
	bool mShowPerformance = false;

	set<fs::path> mStackFolders;

	void ScanFolders() {
		fs::path dataPath;
		string tmp;
		if (mScene->Instance()->GetOption("dataPath", tmp))
				dataPath = tmp;
		else {
			vector<fs::path> defaultLocations { "/Data", "/data", "~/Data", "~/data", "C:/Data", "D:/Data", "E:/Data", "F:/Data" "G:/Data", };
			auto it = defaultLocations.begin();
			while (!fs::exists(dataPath) && it != defaultLocations.end()) dataPath = *it++;
			if (!fs::exists(dataPath))
				throw invalid_argument("could not locate image data path. Specify with --dataPath <path>");
		}

		for (const auto& p : fs::recursive_directory_iterator(dataPath)) {
			if (!p.is_directory() || p.path().stem() == "_mask" || ImageLoader::FolderStackType(p.path()) == ImageStackType::eNone) continue;
			mStackFolders.insert(p.path());
		}
	}
	void LoadScene() {
		fs::path filename;
		string tmp;
		if (mScene->Instance()->GetOption("environment", tmp))
			filename = tmp;

		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		string err;
		string warn;
		if (
			(filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
			(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) ) {
			fprintf_color(ConsoleColorBits::eYellow, stderr, "%s: %s\n", filename.string().c_str(), err.c_str());
			return;
		}
		if (!warn.empty()) fprintf_color(ConsoleColorBits::eYellow, stderr, "%s: %s\n", filename.string().c_str(), warn.c_str());
		
		Device& device = mScene->Instance()->Device();
		vector<shared_ptr<Texture>> images;
		vector<shared_ptr<Material>> materials;
		vector<vector<shared_ptr<Mesh>>> meshes;  // meshes from glTF primitives
		unordered_map<uint32_t, Mesh::VertexAttribute> accessorMap;
		static const map<string, VertexAttributeType> semanticMap {
			{ "position", VertexAttributeType::ePosition },
			{ "normal", VertexAttributeType::eNormal },
			{ "tangent", VertexAttributeType::eTangent },
			{ "bitangent", VertexAttributeType::eBitangent },
			{ "texcoord", VertexAttributeType::eTexcoord },
			{ "color", VertexAttributeType::eColor },
			{ "psize", VertexAttributeType::ePointSize },
			{ "pointsize", VertexAttributeType::ePointSize }
		};

		// images+materials
		for (const auto& i : model.images)
			images.push_back(make_shared<Texture>(i.name, device, vk::Extent3D(i.width, i.height, 1), gltf2vk(i.pixel_type, i.component), (void*)i.image.data(), i.image.size(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst, 0));
		for (const auto& m : model.materials) {
			auto mat = make_shared<Material>(m.name, device->LoadAsset<Pipeline>("Assets/Shaders/pbr.stmb", "pbr"));
			// TODO: read materials
			materials.push_back(mat);
		}

		// build accessorMap
		for (const auto& m : model.meshes)
			for (const auto& prim : m.primitives) {
				uint32_t elementOffset = 0;
				for (const auto& [attribName, accessorIndex] : prim.attributes) {
					if (accessorMap.count(accessorIndex)) continue;

					const auto& accessor = model.accessors[accessorIndex];

					uint32_t elementStride = 0;
					switch (accessor.componentType) {
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: 	elementStride = sizeof(uint8_t); break;
						case TINYGLTF_COMPONENT_TYPE_BYTE: 						elementStride = sizeof(int8_t); break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: 	elementStride = sizeof(uint16_t); break;
						case TINYGLTF_COMPONENT_TYPE_SHORT: 					elementStride = sizeof(int16_t); break;
						case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: 		elementStride = sizeof(uint32_t); break;
						case TINYGLTF_COMPONENT_TYPE_INT: 						elementStride = sizeof(int32_t); break;
						case TINYGLTF_COMPONENT_TYPE_FLOAT: 					elementStride = sizeof(float); break;
						case TINYGLTF_COMPONENT_TYPE_DOUBLE: 					elementStride = sizeof(double); break;
					}
					switch (accessor.type) {
						case TINYGLTF_TYPE_SCALAR:  elementStride *= 1; break;
						case TINYGLTF_TYPE_VEC2: 		elementStride *= 2; break;
						case TINYGLTF_TYPE_VEC3: 		elementStride *= 3; break;
						case TINYGLTF_TYPE_VEC4: 		elementStride *= 4; break;
					}
					
					// parse typename & typeindex
					string typeName;
					uint32_t typeIdx = 0;
					ranges::transform(attribName.begin(), attribName.end(), typeName.begin(), [&](char c) { return tolower(c); });
					size_t c = typeName.find_first_of("0123456789");
					if (c != string::npos) {
						typeIdx = stoi(typeName.substr(c));
						typeName = typeName.substr(0, c);
					}
					if (typeName.back() == '_') typeName = typeName.substr(0, typeName.length() - 1);
					VertexAttributeType type = semanticMap.count(typeName) ? semanticMap.at(typeName) : VertexAttributeType::eOther;
					

					accessorMap.emplace(accessorIndex, Mesh::VertexAttribute(ArrayBufferView(nullptr, 0, elementStride), elementOffset, type, typeIndex, vk::InputRate::eVertex));
					elementOffset += elementStride;
				}
			}

		// meshes
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
				
				auto mesh = make_shared<Mesh>(m.name, topo);
				mesh->SetIndexBuffer(accessorMap.at(prim.indices));
				
				uint32_t vertexCount = 0;
				for (const auto& [attribName, accessorIndex] : prim.attributes) {
					mesh->SetVertexAttribute(accessorMap.at(accessorIndex));
					vertexCount = max(vertexCount, (uint32_t)model.accessors[accessorIndex].count);
				}

				if (!mesh->GetAttribute(VertexAttributeType::eTexcoord, 0)) {
					shared_ptr<Buffer> b = make_shared<Buffer>(mesh->mName + "/VertexData", device, (sizeof(float4)+sizeof(float2))*vertexCount, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);
					mesh->SetVertexAttribute(VertexAttributeType::eTangent , 0, ArrayBufferView(b, 0, sizeof(float4)), 0);
					mesh->SetVertexAttribute(VertexAttributeType::eTexcoord, 0, ArrayBufferView(b, vertexCount*sizeof(float4), sizeof(float2)), 0);
				} else if (!mesh->GetAttribute(VertexAttributeType::eTangent, 0)) {
					// TODO: generate tangents
					shared_ptr<Buffer> b = make_shared<Buffer>(mesh->mName + "/Tangents", device, sizeof(float4)*vertexCount, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);
					mesh->SetVertexAttribute(VertexAttributeType::eTangent, 0, ArrayBufferView(b, 0, sizeof(float4)), 0);
				}

				mesh->AddSubmesh(Mesh::Submesh(vertexCount, 0, (uint32_t)model.accessors[prim.indices].count, 0, nullptr));
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
					mSceneObjects.emplace_back(renderer);
					renderer->Mesh(meshes[node.mesh][primIndex]);
					renderer->Material(materials[prim.material]);
					if (node.translation.size() == 3)
						renderer->LocalPosition((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]);
					if (node.rotation.size() == 4)
						renderer->LocalRotation(fquat((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]));
					if (node.scale.size() == 3)
						renderer->LocalScale((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
					primIndex++;
				}
			}
	}

protected:
	PLUGIN_EXPORT bool OnSceneInit(Scene* scene) override {
		mScene = scene;
		mKeyboardInput = mScene->Instance()->InputManager().GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;
		
		mMainCamera = unique_ptr<Camera>(mScene->CreateObject<Camera>("Camera", set<RenderTargetIdentifier> { "stm_main_render", "stm_main_resolve" "stm_main_depth" }));
		mMainCamera->Near(.00625f);
		mMainCamera->Far(1024.f);
		mMainCamera->FieldOfView(radians(65.f));
		mMainCamera->LocalPosition(0, 1.6f, -mZoom);

		mScene->AmbientLight(.5f);

		auto[extent, usageFlags] = mScene->GetAttachmentInfo("stm_main_resolve");
		mScene->SetAttachmentInfo("stm_main_resolve", extent, usageFlags | vk::ImageUsageFlagBits::eStorage);

		ScanFolders();
		LoadScene();

		return true;
	}
	PLUGIN_EXPORT void OnUpdate(CommandBuffer& commandBuffer) override {
		if (mKeyboardInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->ScrollDelta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->ScrollDelta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);
			}
			if (mVolume && mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x - mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f) {
					mVolume->LocalRotation(fquat::AxisAngle(-normalize(axis), length(axis) * .003f) * mVolume->LocalRotation());
				}
			}
		}
	}
	PLUGIN_EXPORT void OnLateUpdate(CommandBuffer& commandBuffer) override { if (mVolume) mVolume->BakeRender(commandBuffer); }
	
	PLUGIN_EXPORT void OnGui(CommandBuffer& commandBuffer, GuiContext& gui) override {		
		bool worldSpace = camera.StereoMode() != StereoMode::eNone;

		// Draw performance overlay
		if (mShowPerformance && !worldSpace)
			Profiler::DrawGui(gui, (uint32_t)mScene->FPS());

		if (worldSpace)
			gui.BeginWorldLayout(GuiContext::LayoutAxis::eVertical, float4x4::TRS(float3(-.85f, 1, 0), fquat(0, 0, 0, 1), .001f), Rect2D(0, 0, 300, 850));
		else
			gui.BeginScreenLayout(GuiContext::LayoutAxis::eVertical, Rect2D(10, (float)mScene->Instance()->Window()->SwapchainExtent().height - 450 - 10, 300, 450));


		gui.LayoutTitle("Load Dataset");
		gui.LayoutSeparator();
		float prev = gui.mLayoutStyle.mControlSize;
		gui.mLayoutStyle.mControlSize = 24;
		gui.BeginScrollSubLayout(175, mStackFolders.size() * (gui.mLayoutStyle.mControlSize + 2*gui.mLayoutStyle.mControlPadding));
		
		for (const auto& folder : mStackFolders)
			if (gui.LayoutTextButton(folder.stem().string(), TextAnchor::eMin)) {
				commandBuffer.Device().Flush();
				mVolume = unique_ptr<RenderVolume>(mScene->CreateObject<RenderVolume>("Dicom Volume", commandBuffer.Device(), folder));
				mVolume->LocalPosition(mMainCamera->WorldPosition() + mMainCamera->WorldRotation() * float3(0,0,0.5f));
			}

		gui.mLayoutStyle.mControlSize = prev;
		gui.EndLayout();

		if (mVolume) mVolume->DrawGui(commandBuffer, camera, gui);

		gui.EndLayout();
	}

	PLUGIN_EXPORT void OnPostProcess(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, const set<Camera*>& cameras) override {
		if (!mVolume) return;
		for (Camera* camera : cameras)
			mVolume->Draw(commandBuffer, framebuffer, *camera);
	}
};

}

ENGINE_PLUGIN(dcmvs::DicomVis)