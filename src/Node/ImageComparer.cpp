#include "ImageComparer.hpp"
#include "Application.hpp"
#include "Inspector.hpp"
#include "BDPT.hpp"

namespace stm {

void inspector_gui_fn(Inspector& inspector, ImageComparer* v) { v->inspector_gui(); }

ImageComparer::ImageComparer(Node& node) : mNode(node) {
	component_ptr<Inspector> gui = mNode.node_graph().find_components<Inspector>().front();
	gui->register_inspector_gui_fn<ImageComparer>(&inspector_gui_fn);

	auto app = mNode.find_in_ancestor<Application>();

	app->OnUpdate.add_listener(mNode, [&, app](CommandBuffer& commandBuffer, float deltaTime) {
		if (mComparing.empty()) return;
		ProfilerRegion ps("ImageComparer");
		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();

		if (ImGui::Begin("Compare Result")) {
			if (mComparing.size() == 1) {
				Image::View& img = mImages.at(*mComparing.begin());
				const uint32_t w = ImGui::GetWindowSize().x;
				ImGui::Image(&img, ImVec2(w, w * (float)img.extent().height / (float)img.extent().width));
			} else {

			}
		}

		ImGui::End();

	}, Node::EventPriority::eAlmostFirst);
}

void ImageComparer::inspector_gui() {
	if (ImGui::Button("Clear"))
		mImages.clear();

	static char label[64];
	ImGui::InputText("", label, sizeof(label));
	ImGui::SameLine();
	if (ImGui::Button("Save") && !string(label).empty()) {
		component_ptr<BDPT> renderer = mNode.node_graph().find_components<BDPT>().front();
		mImages.emplace(label, renderer->prev_radiance_image());
	}

	for (auto it = mImages.begin(); it != mImages.end(); ) {
		ImGui::PushID(("x" + it->first).c_str());
		const bool d = ImGui::Button("x");
		bool c = mComparing.find(it->first) != mComparing.end();
		ImGui::PopID();
		if (d) {
			if (c) mComparing.erase(it->first);
			it = mImages.erase(it);
		} else {
			ImGui::SameLine();
			if (ImGui::Checkbox(it->first.c_str(), &c)) {
				if (c)
					mComparing.emplace(it->first);
				else
					mComparing.erase(it->first);
			}
			it++;
		}
	}
}

}