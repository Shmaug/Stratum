#include "ImageComparer.hpp"
#include "Application.hpp"
#include "Inspector.hpp"
#include "VCM.hpp"
#include "BDPT.hpp"

#include <Shaders/image_compare.h>

namespace stm {

void inspector_gui_fn(Inspector& inspector, ImageComparer* v) { v->inspector_gui(); }

ImageComparer::ImageComparer(Node& node) : mNode(node) {
	component_ptr<Inspector> gui = mNode.node_graph().find_components<Inspector>().front();
	gui->register_inspector_gui_fn<ImageComparer>(&inspector_gui_fn);

	auto app = mNode.find_in_ancestor<Application>();

	mOffset = float2::Constant(0);
	mZoom = 1.f;

	app->OnUpdate.add_listener(mNode, [&, app](CommandBuffer& commandBuffer, float deltaTime) {
		ProfilerRegion ps("ImageComparer");
		for (auto&[original, img] : mImages|views::values) {
			if (!img) {
				img = make_shared<Image>(commandBuffer.mDevice, original.image()->name(), original.extent(), original.image()->format(), original.image()->layer_count(), 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
				commandBuffer.copy_image(original, img);
				img.image()->generate_mip_maps(commandBuffer);
			}
		}

		if (mComparing.empty()) return;

		auto gui = app.node().find_in_descendants<Gui>();
		if (!gui) return;
		gui->set_context();

		if (ImGui::Begin("Compare Result")) {
			if (mComparing.size() == 1) {
				mCurrent = *mComparing.begin();
			} else {
				bool s = false;
				for (auto c : mComparing) {
					if (s) ImGui::SameLine();
					s = true;
					if (ImGui::Button(c.c_str()))
						mCurrent = c;
				}

				if (mComparing.size() == 2) {
					auto& mse = mMSE[ *mComparing.begin() + "_" + *(++mComparing.begin())];

					bool update = !mse;
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80);
					if (ImGui::DragScalar("Quantization", ImGuiDataType_U32, &mMSEQuantization)) update = true;
					ImGui::SameLine();
					ImGui::SetNextItemWidth(160);
					if (Gui::enum_dropdown("Error Mode", mMSEMode, (uint32_t)ErrorMode::eErrorModeCount, [](uint32_t i) { return to_string((ErrorMode)i); })) update = true;

					if (update) {
						if (!mse)
							mse = make_shared<Buffer>(commandBuffer.mDevice, "MSE", 2*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);

						if (!mImageComparePipeline)
							mImageComparePipeline = make_shared<ComputePipelineState>("mse", make_shared<Shader>(commandBuffer.mDevice, "Shaders/image_compare.spv"));

						{
							ProfilerRegion ps("Compute MSE", commandBuffer);
							mImageComparePipeline->specialization_constant<uint32_t>("gMode") = mMSEMode;
							mImageComparePipeline->specialization_constant<uint32_t>("gQuantization") = mMSEQuantization;
							mImageComparePipeline->descriptor("gImage1") = image_descriptor(mImages.at(*mComparing.begin()).second, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
							mImageComparePipeline->descriptor("gImage2") = image_descriptor(mImages.at(*++mComparing.begin()).second, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
							mImageComparePipeline->descriptor("gOutput") = mse;
							mse[0] = 0;
							mse[1] = 0;
							commandBuffer.bind_pipeline(mImageComparePipeline->get_pipeline());
							mImageComparePipeline->bind_descriptor_sets(commandBuffer);
							commandBuffer.dispatch_over(mImages.at(*mComparing.begin()).second.extent());
						}
					}

					if (mse && !mse.buffer()->in_use()) {
						ImGui::SameLine();
						if (mse[1])
							ImGui::Text("OVERFLOW");
						else
							ImGui::Text("%f", mMSEMode == (uint32_t)ErrorMode::eAverageLuminance || mMSEMode == (uint32_t)ErrorMode::eAverageRGB ? mse[0]/(float)mMSEQuantization : sqrt(mse[0]/(float)mMSEQuantization));
					}
				}
			}


			// image pan/zoom
			const uint32_t w = ImGui::GetWindowSize().x - 4;
			Image::View& img = mImages.at(mCurrent).second;
			const float aspect = (float)img.extent().height / (float)img.extent().width;
			ImVec2 uvMin(mOffset[0], mOffset[1]);
			ImVec2 uvMax(mOffset[0] + mZoom, mOffset[1] + mZoom);
			ImGui::Image(&img, ImVec2(w, w * aspect), uvMin, uvMax);
			if (ImGui::IsItemHovered()) {
				mZoom *= 1 - 0.05f*app->window().input_state().scroll_delta();
				mZoom = clamp(mZoom, 0.f, 1.f);
				mOffset[0] = clamp(mOffset[0], 0.f, 1 - mZoom);
				mOffset[1] = clamp(mOffset[1], 0.f, 1 - mZoom);
				if (app->window().input_state().pressed(KeyCode::eMouse1)) {
					mOffset[0] -= (uvMax.x - uvMin.x) * app->window().input_state().cursor_delta()[0] / w;
					mOffset[1] -= (uvMax.y - uvMin.y) * app->window().input_state().cursor_delta()[1] / (w*aspect);
					mOffset[0] = clamp(mOffset[0], 0.f, 1 - mZoom);
					mOffset[1] = clamp(mOffset[1], 0.f, 1 - mZoom);
				}
			}
		}

		ImGui::End();

	}, Node::EventPriority::eAlmostFirst);
}

void ImageComparer::inspector_gui() {
	if (ImGui::Button("Clear")) {
		mImages.clear();
		mComparing.clear();
		mCurrent.clear();
	}

	static char label[64];
	ImGui::InputText("", label, sizeof(label));
	ImGui::SameLine();
	if (ImGui::Button("Save") && !string(label).empty()) {
		Image::View img;
		if (mNode.node_graph().component_count<VCM>())
			img = mNode.node_graph().find_components<VCM>().front()->prev_result();
		else if (mNode.node_graph().component_count<BDPT>())
			img = mNode.node_graph().find_components<BDPT>().front()->prev_result();
		mImages.emplace(label, pair<Image::View, Image::View>{ img, {} });
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