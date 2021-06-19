#include "VolumeRenderer.hpp"

namespace dcmvs {

class DicomVis {
private:
	NodeGraph::Node& mNode;
	unordered_set<fs::path> mStackFolders;

	NodeGraph::Node* mVolumeNode;
	VolumeRenderer* mVolume;

	inline void scan_folders() {
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
	PLUGIN_EXPORT DicomVis(NodeGraph::Node& node) : mNode(node) {
		auto[extent, usageFlags] = mNodeGraph->GetAttachmentInfo("stm_main_resolve");
		mNodeGraph->SetAttachmentInfo("stm_main_resolve", extent, usageFlags | vk::ImageUsageFlagBits::eStorage);

		scan_folders();
	}

	PLUGIN_EXPORT void update(CommandBuffer& commandBuffer, float deltaTime) {
		if (!mVolume) return;
		if (!ImGui::GetIO()->WantCaptureMouse) {
			if (window.pressed(KeyCode::eMouse1)) {
				Vector3f v0 = transform_vector(cameraTransform, Vector3f(-window.cursor_pos().y, window.cursor_pos().x, 0));
				Vector3f v1 = transform_vector(cameraTransform, Vector3f(-window.cursor_pos_last().y, window.cursor_pos_last().x, 0));
				Vector3f axis = v0.cross(v1);
				float angle = axis.norm();
				if (angle > .001f) {
					mVolume->LocalRotation(Quaternionf::AxisAngle(-axis/angle, angle * .003f) * volumeRotation);
				}
			}
		}
		mVolume->bake(commandBuffer); 
	}
	
	PLUGIN_EXPORT void imgui() {	
		if (ImGui::Begin("Volume Visualization")) {
			if (ImGui::BeginChild("Load Dataset", {0.f, 200.f})) {
				for (const auto& folder : mStackFolders)
					if (ImGui::Button(folder.stem().string())) {
						mDevice.flush();
						mNode.erase<VolumeRenderer>();
						mVolume = mNode.emplace<VolumeRenderer>("Volume", mDevice, folder));
						mVolume->LocalPosition(transform_point(cameraTransform, Vector3f(0,0,0.5f)));
					}
			}
			ImGui::EndChild();
			if (mVolume) mVolume->imgui();
		}
		ImGui::End();
	}

	PLUGIN_EXPORT void draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer) {
		if (mVolume) mVolume->draw(commandBuffer, framebuffer);
	}
};

}