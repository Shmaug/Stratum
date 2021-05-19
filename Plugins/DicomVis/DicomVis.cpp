#include <NodeGraph/PluginModule.hpp>
#include <NodeGraph/RenderNode.hpp>

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
	NodeGraph* mNodeGraph = nullptr;
	MouseKeyboardInput* mKeyboardInput = nullptr;
	
	vector<unique_ptr<Object>> mSceneObjects;
	unique_ptr<Camera> mMainCamera;
	unique_ptr<RenderVolume> mVolume;

	float mZoom = 0;
	bool mShowPerformance = false;

	unordered_set<fs::path> mStackFolders;

	void ScanFolders() {
		fs::path dataPath;
		string tmp;
		if (mNodeGraph->mInstance->GetOption("dataPath", tmp))
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
			mStackFolders.emplace(p.path());
		}
	}

protected:
	PLUGIN_EXPORT bool OnSceneInit(NodeGraph* scene) override {
		mNodeGraph = scene;
		mKeyboardInput = mNodeGraph->mInstance->InputManager().GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;
		
		mMainCamera = unique_ptr<Camera>(mNodeGraph->CreateObject<Camera>("Camera", unordered_set<RenderAttachmentId> { "stm_main_render", "stm_main_resolve" "stm_main_depth" }));
		mMainCamera->Near(.00625f);
		mMainCamera->Far(1024.f);
		mMainCamera->FieldOfView(radians(65.f));
		mMainCamera->LocalPosition(0, 1.6f, -mZoom);

		mNodeGraph->AmbientLight(.5f);

		auto[extent, usageFlags] = mNodeGraph->GetAttachmentInfo("stm_main_resolve");
		mNodeGraph->SetAttachmentInfo("stm_main_resolve", extent, usageFlags | vk::ImageUsageFlagBits::eStorage);

		ScanFolders();
		LoadScene();

		return true;
	}
	PLUGIN_EXPORT void OnUpdate(CommandBuffer& commandBuffer) override {
		if (mKeyboardInput->is_pressed_redge(KeyCode::eKeyTilde)) mShowPerformance = !mShowPerformance;

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->scroll_delta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->scroll_delta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);
			}
			if (mVolume && mKeyboardInput->is_pressed(KeyCode::eMouse1)) {
				Vector3f axis = mMainCamera->WorldRotation() * Vector3f(0, 1, 0) * mKeyboardInput->cursor_delta().x - mMainCamera->WorldRotation() * Vector3f(1, 0, 0) * mKeyboardInput->cursor_delta().y;
				if (dot(axis, axis) > .001f) {
					mVolume->LocalRotation(fquat::AxisAngle(-normalize(axis), length(axis) * .003f) * mVolume->LocalRotation());
				}
			}
		}
	}
	PLUGIN_EXPORT void OnLateUpdate(CommandBuffer& commandBuffer) override {
		if (mVolume) mVolume->BakeRender(commandBuffer);
	}
	
	PLUGIN_EXPORT void OnPostProcess(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, const unordered_set<Camera*>& cameras) override {
		if (!mVolume) return;
		for (Camera* camera : cameras)
			mVolume->draw(commandBuffer, framebuffer, *camera);
	}

	PLUGIN_EXPORT void OnGui(CommandBuffer& commandBuffer, GuiContext& gui) override {		
		bool worldSpace = camera.StereoMode() != StereoMode::eNone;

		// draw performance overlay
		if (mShowPerformance && !worldSpace)
			Profiler::DrawGui(gui, (uint32_t)mNodeGraph->FPS());

		if (worldSpace)
			gui.BeginWorldLayout(GuiContext::LayoutAxis::eVertical, Matrix4f::TRS(Vector3f(-.85f, 1, 0), fquat::Identity(), .001f), Rect2D(0, 0, 300, 850));
		else
			gui.BeginScreenLayout(GuiContext::LayoutAxis::eVertical, Rect2D(10, (float)mNodeGraph->mInstance->window()->swapchain_extent().height - 450 - 10, 300, 450));


		gui.LayoutTitle("Load Dataset");
		gui.LayoutSeparator();
		float prev = gui.mLayoutStyle.mControlSize;
		gui.mLayoutStyle.mControlSize = 24;
		gui.BeginScrollSubLayout(175, mStackFolders.size() * (gui.mLayoutStyle.mControlSize + 2*gui.mLayoutStyle.mControlPadding));
		
		for (const auto& folder : mStackFolders)
			if (gui.LayoutTextButton(folder.stem().string(), TextAnchor::eMin)) {
				commandBuffer.mDevice.flush();
				mVolume = unique_ptr<RenderVolume>(mNodeGraph->CreateObject<RenderVolume>("Dicom Volume", commandBuffer.mDevice, folder));
				mVolume->LocalPosition(mMainCamera->WorldPosition() + mMainCamera->WorldRotation() * Vector3f(0,0,0.5f));
			}

		gui.mLayoutStyle.mControlSize = prev;
		gui.EndLayout();

		if (mVolume) mVolume->DrawGui(commandBuffer, camera, gui);

		gui.EndLayout();
	}
};

}

ENGINE_PLUGIN(dcmvs::DicomVis)