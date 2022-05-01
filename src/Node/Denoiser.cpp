#include "Denoiser.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <random>

#include <Shaders/filter_type.h>

namespace stm {

void inspector_gui_fn(Denoiser* v) { v->on_inspector_gui(); }

Denoiser::Denoiser(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);

	create_pipelines();
}

void Denoiser::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	auto samplerClamp = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings;
	auto process_shader = [&](shared_ptr<ComputePipelineState>& dst, const string& shader_name) {
		auto shader = make_shared<Shader>(instance->device(), "Shaders/" + shader_name +".spv");
		dst = make_shared<ComputePipelineState>(shader_name, shader);
		for (const auto[name,binding] : shader->descriptors()) {
			DescriptorSetLayout::Binding b;
			b.mDescriptorType = binding.mDescriptorType;
			b.mDescriptorCount = 1;
			for (const auto& s : binding.mArraySize) {
				if (s.index() == 0)
					b.mDescriptorCount *= get<0>(s);
				else
					printf_color(ConsoleColor::eYellow, "Warning: variable descriptor set size not supported yet\n");
			}
			if (name == "gSampler" || name == "gSampler1") b.mImmutableSamplers = { samplerRepeat };
			if (name == "gVolumes" || name == "gImages" || name == "g3DImages") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
			b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
			bindings.emplace(binding.mBinding, b);
			mDescriptorMap.emplace(name, binding.mBinding);
		}
	};
	process_shader(mTemporalAccumulationPipeline, "temporal_accumulation");
	process_shader(mEstimateVariancePipeline, "estimate_variance");
	process_shader(mAtrousPipeline, "atrous");
	process_shader(mCopyRGBPipeline, "atrous_copy_rgb");
	mDescriptorSetLayout = make_shared<DescriptorSetLayout>(instance->device(), "denoiser_descriptor_set_layout", bindings);

	mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit") = 128;
	mTemporalAccumulationPipeline->set_immutable_sampler("gSampler", samplerClamp);
	mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost") = 3;
}

void Denoiser::on_inspector_gui() {
	if (mTemporalAccumulationPipeline && ImGui::Button("Reload Shaders")) {
		mTemporalAccumulationPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	ImGui::Checkbox("Reprojection", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gReprojection")));

	ImGui::PushItemWidth(40);
	ImGui::DragFloat("Target Sample Count", &mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit"));
	ImGui::DragScalar("Filter Iterations", ImGuiDataType_U32, &mAtrousIterations, 0.1f);

	if (mAtrousIterations > 0) {
		ImGui::Indent();
		ImGui::DragFloat("Sigma Luminance Boost", &mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost"), .1f, 0, 0, "%.2f");
		ImGui::PopItemWidth();
		const uint32_t m = mAtrousPipeline->specialization_constant("gFilterKernelType");
		if (ImGui::BeginCombo("Filter Type", to_string((FilterKernelType)m).c_str())) {
			for (uint32_t i = 0; i < FilterKernelType::eFilterKernelTypeCount; i++)
				if (ImGui::Selectable(to_string((FilterKernelType)i).c_str(), m == i))
					mAtrousPipeline->specialization_constant("gFilterKernelType") = i;
			ImGui::EndCombo();
		}
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("History Tap Iteration", ImGuiDataType_U32, &mHistoryTap, 0.1f);
		ImGui::Unindent();
	}
	ImGui::PopItemWidth();
}

Image::View Denoiser::denoise(CommandBuffer& commandBuffer, const Image::View& radiance, const Buffer::View<ViewData>& views, const Buffer::View<VisibilityInfo>& visibility) {
	ProfilerRegion ps("Denoiser::denoise", commandBuffer);

	// Initialize buffers

	{
		ProfilerRegion ps("Allocate Frame Resources", commandBuffer);
		if (mCurFrame) {
			mFrameResources.push_back(mCurFrame);
			mPrevFrame = mCurFrame;
		}
		mCurFrame.reset();

		// reuse old frame resources
		for (auto it = mFrameResources.begin(); it != mFrameResources.end(); it++) {
			if (*it != mPrevFrame && (*it)->mFence->status() == vk::Result::eSuccess) {
				mCurFrame = *it;
				mFrameResources.erase(it);
				break;
			}
		}
		if (!mCurFrame) mCurFrame = make_shared<FrameResources>();

		mCurFrame->mFence = commandBuffer.fence();
	}

	mCurFrame->mViews = views;
	mCurFrame->mRadiance = radiance;
	mCurFrame->mVisibility = visibility;

	const vk::Extent3D extent = radiance.extent();
	if (!mCurFrame->mAccumColor || mCurFrame->mAccumColor.extent() != extent) {
		ProfilerRegion ps("Create images");

		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", extent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		for (uint32_t i = 0; i < mCurFrame->mTemp.size(); i++)
			mCurFrame->mTemp[i] = make_shared<Image>(commandBuffer.mDevice, "pingpong" + to_string(i), extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);

		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
	}

	Image::View output = radiance;

	bool history_valid = mPrevFrame && mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();
	if (history_valid && !mTemporalAccumulationPipeline->specialization_constant("gReprojection"))
		if ((mCurFrame->mViews[0].camera_to_world.m != mPrevFrame->mViews[0].camera_to_world.m).any())
			history_valid = false;

	if (history_valid) {
		mCurFrame->mDescriptorSet = make_shared<DescriptorSet>(mDescriptorSetLayout, "denoiser_view_descriptors");
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gViews"), views);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gVisibility"), mCurFrame->mVisibility);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevVisibility"), mPrevFrame->mVisibility);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gAccumColor"), image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gAccumMoments"), image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevAccumColor"), image_descriptor(mPrevFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevAccumMoments"), image_descriptor(mPrevFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gFilterImages"), 0, image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gFilterImages"), 1, image_descriptor(mCurFrame->mTemp[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gInstanceIndexMap"), mNode.find_in_ancestor<Scene>()->data()->mInstanceIndexMap);

		output = mCurFrame->mAccumColor;

		commandBuffer.barrier({ mCurFrame->mVisibility }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mDescriptorSet->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);

			mTemporalAccumulationPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();

			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline(mDescriptorSetLayout));
			commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		if (mAtrousIterations > 0) {
			{ // estimate variance
				ProfilerRegion ps("Estimate Variance", commandBuffer);

				mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mAccumColor.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mAccumMoments.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

				commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline(mDescriptorSetLayout));
				commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
				mTemporalAccumulationPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(extent);
			}

			ProfilerRegion ps("Filter image", commandBuffer);
			mAtrousPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
			commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline(mDescriptorSetLayout));
			commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
			mAtrousPipeline->push_constants(commandBuffer);

			for (uint32_t i = 0; i < mAtrousIterations; i++) {
				mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mTemp[1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

				commandBuffer.push_constant<uint32_t>("gIteration", i);
				commandBuffer.push_constant<uint32_t>("gStepSize", 1 << i);
				if (i > 0) mAtrousPipeline->transition_images(commandBuffer);
				commandBuffer.dispatch_over(extent);

				if (i+1 == mHistoryTap) {
					// copy rgb (keep w) to AccumColor
					mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mTemp[1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

					commandBuffer.bind_pipeline(mCopyRGBPipeline->get_pipeline(mDescriptorSetLayout));
					commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
					mCopyRGBPipeline->transition_images(commandBuffer);
					commandBuffer.dispatch_over(extent);

					if (i+1 < mAtrousIterations) {
						commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline(mDescriptorSetLayout));
						commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
						mAtrousPipeline->push_constants(commandBuffer);
					}
				}
			}
			output = mCurFrame->mTemp[mAtrousIterations%2];
		}
	} else
		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });

	return output;
}

}