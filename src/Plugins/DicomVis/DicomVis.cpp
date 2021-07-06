#include "VolumeRenderer.hpp"
#include "ImageLoader.hpp"
#include "../../Core/Window.hpp"

namespace dcmvs {

class DicomVis {
private:
	NodeGraph::Node& mNode;
	unordered_set<fs::path> mStackFolders;

	VolumeRenderer* mVolume = nullptr;

	inline void scan_folders(const fs::path& dataPath) {
		if (fs::exists(dataPath))
			for (const auto& p : fs::recursive_directory_iterator(dataPath)) {
				if (!p.is_directory() || p.path().stem() == "_mask" || FolderStackType(p.path()) == ImageStackType::eNone) continue;
				mStackFolders.emplace(p.path());
			}
	}

protected:
	PLUGIN_API DicomVis(NodeGraph::Node& node) : mNode(node) {
		if (auto paths = mNode.get<Instance>().find_arguments("dataPath"); !paths.empty())
			for (const auto& p : paths)
				scan_folders(p);
		else {
			vector<fs::path> defaultPaths { "/Data", "/data", "~/Data", "~/data", "C:/Data", "D:/Data", "E:/Data", "F:/Data" "G:/Data" };
			for (const auto& p : defaultPaths)
				scan_folders(p);
		}
	}

	PLUGIN_API void update(CommandBuffer& commandBuffer, float deltaTime) {
		if (!mVolume) return;
		if (!ImGui::GetIO().WantCaptureMouse) {
			Window& window = mNode.get<Instance>().window();
			if (window.pressed(KeyCode::eMouse1)) {
				Vector3f v0 = transform_vector(cameraTransform, Vector3f(-window.cursor_pos().y(), window.cursor_pos().x(), 0));
				Vector3f v1 = transform_vector(cameraTransform, Vector3f(-window.cursor_pos_last().y(), window.cursor_pos_last().x(), 0));
				Vector3f axis = v0.cross(v1);
				float angle = axis.norm();
				if (angle > .001f) {
					hlsl::quatf& volumeRotation = mVolume->mNode.get<hlsl::TransformData>().Rotation;
					volumeRotation = AngleAxisf(angle * .003f, -axis/angle) * volumeRotation;
				}
			}
		}
		mVolume->bake(commandBuffer); 
	}
	
	PLUGIN_API void imgui() {	
		if (ImGui::Begin("Volume Visualization")) {
			if (ImGui::BeginChild("Load Dataset", {0.f, 200.f})) {
				for (const auto& folder : mStackFolders)
					if (ImGui::Button(folder.stem().string().c_str())) {
						ImageStackType type = FolderStackType(folder);
						Vector3f voxelSize;
						Texture::View volume;
						switch (type) {
						case ImageStackType::eStandard:
							volume = load_volume(folder, commandBuffer, &voxelSize);
							break;
						case ImageStackType::eDicom:
							volume = load_volume(folder, commandBuffer, &voxelSize);
							break;
						case ImageStackType::eRaw:
							volume = load_volume(folder, commandBuffer, &voxelSize);
							break;
						}
						if (volume) {
							mNode.erase<VolumeRenderer>();
							mNode.erase<hlsl::TransformData>();
							Texture::View mask = LoadStandardStack((folder / "_mask").string(), commandBuffer, nullptr, true, 1, false);
							mVolume = &mNode.make_component<VolumeRenderer>("Volume", volume, mask, voxelSize);
							mNode.make_component<hlsl::TransformData>();
						}
					}
			}
			ImGui::EndChild();
			if (mVolume) mVolume->imgui();
		}
		ImGui::End();
	}
};

}