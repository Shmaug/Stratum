#include "Denoiser.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <random>

#include <Shaders/filter_type.h>

namespace stm {

void inspector_gui_fn(Inspector& inspector, Denoiser* v) { v->on_inspector_gui(); }

Denoiser::Denoiser(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);

	create_pipelines();
}

void Denoiser::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gStaticSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	auto samplerClamp = make_shared<Sampler>(instance->device(), "gStaticSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings;
	auto process_shader = [&](shared_ptr<ComputePipelineState>& dst, const fs::path& path, const string& entry_point = "", const vector<string>& compile_args = {}) {
		shared_ptr<Shader> shader;
		if (entry_point.empty()) {
			shader = make_shared<Shader>(instance->device(), path);
			dst = make_shared<ComputePipelineState>(path.stem().string(), shader);
		} else {
			shader = make_shared<Shader>(instance->device(), path, entry_point, compile_args);
			dst = make_shared<ComputePipelineState>(path.stem().string() + "_" + entry_point, shader);
		}

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
			if (name == "gStaticSampler" || name == "gStaticSampler1") b.mImmutableSamplers = { samplerRepeat };
			if (name == "gVolumes" || name == "gImages" || name == "gImage1s") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
			b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
			bindings.emplace(binding.mBinding, b);
			mDescriptorMap.emplace(name, binding.mBinding);
		}
	};
	/*
	process_shader(mTemporalAccumulationPipeline, "Shaders/temporal_accumulation.spv");
	process_shader(mEstimateVariancePipeline, "Shaders/estimate_variance.spv");
	process_shader(mAtrousPipeline, "Shaders/atrous.spv");
	process_shader(mCopyRGBPipeline, "Shaders/atrous_copy_rgb.spv");
	/*/
	process_shader(mTemporalAccumulationPipeline, "../../src/Shaders/kernels/temporal_accumulation.hlsl", "main");
	process_shader(mEstimateVariancePipeline    , "../../src/Shaders/kernels/estimate_variance.hlsl", "main");
	process_shader(mAtrousPipeline              , "../../src/Shaders/kernels/atrous.hlsl", "main");
	process_shader(mCopyRGBPipeline             , "../../src/Shaders/kernels/atrous.hlsl", "copy_rgb");
	//*/
	mDescriptorSetLayout = make_shared<DescriptorSetLayout>(instance->device(), "denoiser_descriptor_set_layout", bindings);

	mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit") = 0;
	mTemporalAccumulationPipeline->set_immutable_sampler("gStaticSampler", samplerClamp);
	mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost") = 3;
	mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gReprojection") = 1;
	mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gDemodulateAlbedo") = 1;
}

void Denoiser::on_inspector_gui() {
	if (mTemporalAccumulationPipeline && ImGui::Button("Reload Denoiser Shaders")) {
		auto instance = mNode.find_in_ancestor<Instance>();
		instance->device()->waitIdle();
		create_pipelines();
	}

	ImGui::SetNextItemWidth(200);
	Gui::enum_dropdown("Denoiser Debug Mode", mDebugMode, (uint32_t)DenoiserDebugMode::eDebugModeCount, [](uint32_t i) { return to_string((DenoiserDebugMode)i); });

	ImGui::Text("%u frames accumulated", mAccumulatedFrames);
	ImGui::SameLine();
	if (ImGui::Button("Reset Accumulation"))
		reset_accumulation();

	ImGui::Checkbox("Reprojection", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gReprojection")));
	if (ImGui::Checkbox("Demodulate Albedo", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gDemodulateAlbedo"))))
		mResetAccumulation = true;

	ImGui::PushItemWidth(40);
	ImGui::DragFloat("Target Sample Count", &mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit"));
	ImGui::DragScalar("Filter Iterations", ImGuiDataType_U32, &mAtrousIterations, 0.1f);

	if (mAtrousIterations > 0) {
		ImGui::Indent();
		ImGui::DragFloat("Variance Boost Frames", &mEstimateVariancePipeline->push_constant<float>("gVarianceBoostLength"));
		ImGui::DragFloat("Sigma Luminance Boost", &mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost"), .1f, 0, 0, "%.2f");
		ImGui::PopItemWidth();
		Gui::enum_dropdown("Filter", mAtrousPipeline->specialization_constant<uint32_t>("gFilterKernelType"), (uint32_t)FilterKernelType::eFilterKernelTypeCount, [](uint32_t i) { return to_string((FilterKernelType)i); });
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("History Tap Iteration", ImGuiDataType_U32, &mHistoryTap, 0.1f);
		ImGui::Unindent();
	}
	ImGui::PopItemWidth();

}

Image::View Denoiser::denoise(
	CommandBuffer& commandBuffer,
	const Image::View& radiance,
	const Image::View& albedo,
	const Buffer::View<ViewData>& views,
	const Buffer::View<VisibilityInfo>& visibility,
	const Buffer::View<DepthInfo>& depth,
	const Image::View& prev_uvs) {
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
	mCurFrame->mAlbedo = albedo;
	mCurFrame->mVisibility = visibility;
	mCurFrame->mDepth = depth;

	const vk::Extent3D extent = radiance.extent();
	if (!mCurFrame->mAccumColor || mCurFrame->mAccumColor.extent() != extent) {
		ProfilerRegion ps("Create images");

		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", extent, radiance.image()->format(), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", extent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		for (uint32_t i = 0; i < mCurFrame->mTemp.size(); i++)
			mCurFrame->mTemp[i] = make_shared<Image>(commandBuffer.mDevice, "pingpong" + to_string(i), extent, radiance.image()->format(), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDebugImage = make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, radiance.image()->format(), 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc);

		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
		commandBuffer.clear_color_image(mCurFrame->mAccumMoments, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
	}

	Image::View output = radiance;

	mTemporalAccumulationPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
	//mEstimateVariancePipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
	//mAtrousPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;

	if (commandBuffer.mDevice.mInstance.window().pressed_redge(KeyCode::eKeyF5))
		mResetAccumulation = true;

	if (!mResetAccumulation && mPrevFrame && mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent()) {
		mCurFrame->mDescriptorSet = make_shared<DescriptorSet>(mDescriptorSetLayout, "denoiser_view_descriptors");
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gViews"), mCurFrame->mViews);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gInstanceIndexMap"), mNode.find_in_ancestor<Scene>()->data()->mInstanceIndexMap);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gVisibility"), mCurFrame->mVisibility);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevVisibility"), mPrevFrame->mVisibility);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gDepth"), mCurFrame->mDepth);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevDepth"), mPrevFrame->mDepth);
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevUVs"), image_descriptor(prev_uvs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gAccumColor"), image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gAccumMoments"), image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gFilterImages"), 0, image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gFilterImages"), 1, image_descriptor(mCurFrame->mTemp[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevAccumColor"), image_descriptor(mPrevFrame->mAccumColor, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gPrevAccumMoments"), image_descriptor(mPrevFrame->mAccumMoments, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
		mCurFrame->mDescriptorSet->insert_or_assign(mDescriptorMap.at("gDebugImage"), image_descriptor(mCurFrame->mDebugImage, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));

		output = mCurFrame->mAccumColor;

		commandBuffer.barrier({ mCurFrame->mVisibility }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mDescriptorSet->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);

			mTemporalAccumulationPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();

			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline(mDescriptorSetLayout));
			commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Temporal accumulation");
			commandBuffer.dispatch_over(extent);
		}

		if (mAtrousIterations > 0) {
			{ // estimate variance
				ProfilerRegion ps("Estimate variance", commandBuffer);

				mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mAccumColor.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mAccumMoments.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

				commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline(mDescriptorSetLayout));
				commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
				mTemporalAccumulationPipeline->push_constants(commandBuffer);
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Estimate variance");
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
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Atrous filter");
				commandBuffer.dispatch_over(extent);

				if (i+1 == mHistoryTap) {
					// copy rgb (keep w) to AccumColor
					mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mTemp[1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

					commandBuffer.bind_pipeline(mCopyRGBPipeline->get_pipeline(mDescriptorSetLayout));
					commandBuffer.bind_descriptor_set(0, mCurFrame->mDescriptorSet);
					mCopyRGBPipeline->transition_images(commandBuffer);
					commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Copy RGB");
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
		mAccumulatedFrames++;
	} else {
		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
		mResetAccumulation = false;
		mAccumulatedFrames = 0;
	}

	return mDebugMode == DenoiserDebugMode::eNone ? output : mCurFrame->mDebugImage;
}

}