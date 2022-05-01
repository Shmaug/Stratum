#include "BDPT.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

#include <Shaders/tonemap.h>

namespace stm {

void inspector_gui_fn(BDPT* v) { v->on_inspector_gui(); }

BDPT::BDPT(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);
	app->OnUpdate.listen(mNode, bind(&BDPT::update, this, std::placeholders::_1, std::placeholders::_2), EventPriority::eAlmostLast);

#ifdef STRATUM_ENABLE_OPENXR
	auto xrnode = app.node().find_in_descendants<XR>();
	if (xrnode) {
		xrnode->OnRender.listen(mNode, [&, app](CommandBuffer& commandBuffer) {
			vector<ViewData> views;
			views.reserve(xrnode->views().size());
			for (const XR::View& v : xrnode->views())
				views.emplace_back(v.mCamera.view(node_to_world(v.mCamera.node())));
			render(commandBuffer, xrnode->back_buffer(), views);
			xrnode->back_buffer().transition_barrier(commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
			});

		app->OnRenderWindow.listen(mNode, [=](CommandBuffer& commandBuffer) {
			commandBuffer.blit_image(xrnode->back_buffer(), app->window().back_buffer());
			}, EventPriority::eAlmostFirst);
	} else
#endif
	{
		auto scene = mNode.find_in_ancestor<Scene>();
		app->OnRenderWindow.listen(mNode, [&, app, scene](CommandBuffer& commandBuffer) {
			render(commandBuffer, app->window().back_buffer(), { scene->mMainCamera->view(node_to_world(scene->mMainCamera.node())) });
		});
	}

	create_pipelines();
}

void BDPT::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	optional<uint32_t> debug_mode;
	optional<uint32_t> bdpt_flags;
	float exposure = 1;
	if (mTraceStepPipeline) {
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		bdpt_flags = mTraceStepPipeline->specialization_constant("gSpecializationFlags");
		debug_mode = mVisibilityPipeline->specialization_constant("gDebugMode");
	} else {
		mPushConstants.gMaxPathVertices = 8;
		mPushConstants.gMinPathVertices = 3;
		mPushConstants.gMaxNullCollisions = 64;
	}

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSamplerRepeat", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings[2];
	auto process_shader = [&](shared_ptr<ComputePipelineState>& dst, const string& shader_name) {
		auto shader = make_shared<Shader>(instance->device(), "Shaders/" + shader_name + ".spv");
		dst = make_shared<ComputePipelineState>(shader_name, shader);
		for (const auto [name, binding] : shader->descriptors()) {
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
			if (name == "gVolumes" || name == "gImages") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
			b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
			bindings[binding.mSet].emplace(binding.mBinding, b);
			mDescriptorMap[binding.mSet].emplace(name, binding.mBinding);
		}
	};
	process_shader(mVisibilityPipeline, "bdpt_visibility");
	process_shader(mTraceStepPipeline, "bdpt_path_step");
	for (uint32_t i = 0; i < 2; i++)
		mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "bdpt_descriptor_set_layout" + to_string(i), bindings[i]);

	if (bdpt_flags) mTraceStepPipeline->specialization_constant("gSpecializationFlags") = *bdpt_flags;
	if (debug_mode) mVisibilityPipeline->specialization_constant("gDebugMode") = *debug_mode;

	mTonemapPipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap.spv"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;

	mRayCount = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	memset(mRayCount.data(), 0, mRayCount.size_bytes());
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
}

void BDPT::on_inspector_gui() {
	if (mTraceStepPipeline && ImGui::Button("Reload Shaders")) {
		mTraceStepPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	ImGui::CheckboxFlags("Count Rays/second", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_COUNT_RAYS);
	if (mTraceStepPipeline->specialization_constant("gSpecializationFlags") & BDPT_FLAG_COUNT_RAYS) {
		const auto [rps, ext] = format_number(mRaysPerSecond);
		ImGui::Text("%.2f%s Rays/second", rps, ext);
	}

	uint32_t m = mVisibilityPipeline->specialization_constant("gDebugMode");
	if (ImGui::BeginCombo("Debug Mode", to_string((DebugMode)m).c_str())) {
		for (uint32_t i = 0; i < (uint32_t)DebugMode::eDebugModeCount; i++)
			if (ImGui::Selectable(to_string((DebugMode)i).c_str(), m == i)) {
				mVisibilityPipeline->specialization_constant("gDebugMode") = i;
			}
		ImGui::EndCombo();
	}

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Path Vertices", ImGuiDataType_U32, &mPushConstants.gMaxPathVertices, 1);
		ImGui::DragScalar("Min Path Vertices", ImGuiDataType_U32, &mPushConstants.gMinPathVertices, 1);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions, 1);
		ImGui::PopItemWidth();

		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("NEE", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_NEE);
		if (mTraceStepPipeline->specialization_constant("gSpecializationFlags") & BDPT_FLAG_NEE) {
			if ((mTraceStepPipeline->specialization_constant("gSpecializationFlags") & BDPT_FLAG_HAS_ENVIRONMENT) && (mTraceStepPipeline->specialization_constant("gSpecializationFlags") & BDPT_FLAG_HAS_EMISSIVES)) {
				ImGui::PushItemWidth(40);
				ImGui::Indent();
				ImGui::DragFloat("Environment Sample Probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
				ImGui::Unindent();
				ImGui::PopItemWidth();
			}
			ImGui::Indent();
			ImGui::CheckboxFlags("Multiple Importance", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_MIS);
			if (mTraceStepPipeline->specialization_constant("gSpecializationFlags") & BDPT_FLAG_HAS_EMISSIVES)
				ImGui::CheckboxFlags("Uniform Sphere Sampling", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
			ImGui::Unindent();
		}
		ImGui::CheckboxFlags("Ray Cone LoD", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_RAY_CONES);
		ImGui::CheckboxFlags("Demodulate Albedo", &mTraceStepPipeline->specialization_constant("gSpecializationFlags"), BDPT_FLAG_DEMODULATE_ALBEDO);
		ImGui::Checkbox("Use Denoiser", &mDenoise);
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		uint32_t m = mTonemapPipeline->specialization_constant("gMode");
		if (ImGui::BeginCombo("Tone mapping", to_string((TonemapMode)m).c_str())) {
			for (uint32_t i = 0; i < (uint32_t)TonemapMode::eTonemapModeCount; i++)
				if (ImGui::Selectable(to_string((TonemapMode)i).c_str(), m == i))
					mTonemapPipeline->specialization_constant("gMode") = i;
			ImGui::EndCombo();
		}
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant("gGammaCorrection")));
	}

	if (mPrevFrame) {
		static char path[256]{ 'i', 'm', 'a', 'g', 'e', '.', 'h', 'd', 'r', '\0' };
		ImGui::InputText("Output HDR", path, sizeof(path));
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			Device& d = mPrevFrame->mRadiance.image()->mDevice;
			auto cb = d.get_command_buffer("image copy");

			Image::View src = mDenoise ? mPrevFrame->mDenoiseResult : mPrevFrame->mRadiance;
			if (src.image()->format() != vk::Format::eR32G32B32A32Sfloat) {
				Image::View tmp = make_shared<Image>(cb->mDevice, "gRadiance", src.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage);
				cb->blit_image(src, tmp);
				src = tmp;
			}

			Buffer::View<float> pixels = make_shared<Buffer>(d, "image copy tmp", src.extent().width * src.extent().height * sizeof(float) * 4, vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
			cb->copy_image_to_buffer(src, pixels);

			d->waitIdle();
			d.submit(cb);
			cb->fence()->wait();

			stbi_write_hdr(path, src.extent().width, src.extent().height, 4, pixels.data());
		}
	}
}

void BDPT::update(CommandBuffer& commandBuffer, const float deltaTime) {
	ProfilerRegion ps("PathTracer::update", commandBuffer);

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

		if (mPrevFrame)
			mCurFrame->mFrameNumber = mPrevFrame->mFrameNumber + 1;
		else
			mCurFrame->mFrameNumber = 0;

		mCurFrame->mFence = commandBuffer.fence();
	}

	mCurFrame->mSceneData = mNode.find_in_ancestor<Scene>()->data();
	if (!mCurFrame->mSceneData) return;

	mPushConstants.gEnvironmentMaterialAddress = mCurFrame->mSceneData->mEnvironmentMaterialAddress;
	mPushConstants.gLightCount = (uint32_t)mCurFrame->mSceneData->mLightInstances.size();

	if (!mCurFrame->mSceneDescriptors) mCurFrame->mSceneDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[0], "path_tracer_scene_descriptors");
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gScene"), **mCurFrame->mSceneData->mScene);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVertices"), mCurFrame->mSceneData->mVertices);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gIndices"), mCurFrame->mSceneData->mIndices);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstances"), mCurFrame->mSceneData->mInstances);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceInverseTransforms"), mCurFrame->mSceneData->mInstanceInverseTransforms);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceMotionTransforms"), mCurFrame->mSceneData->mInstanceMotionTransforms);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gMaterialData"), mCurFrame->mSceneData->mMaterialData);
	//mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gDistributions"), mCurFrame->mSceneData->mDistributionData);
	//mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gLightInstances"), mCurFrame->mSceneData->mLightInstances);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gRayCount"), mRayCount);
	for (const auto& [image, index] : mCurFrame->mSceneData->mResources.images)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto& [vol, index] : mCurFrame->mSceneData->mResources.volume_data_map)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVolumes"), index, vol);
	mCurFrame->mSceneDescriptors->flush_writes();

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mRayCount[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mRayCount[0];
		mRaysPerSecondTimer = 0;
	}
}

void BDPT::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<ViewData>& views) {
	if (!mCurFrame || !mCurFrame->mSceneData) {
		commandBuffer.clear_color_image(renderTarget, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
		return;
	}

	ProfilerRegion ps("BDPT::render", commandBuffer);

	// Initialize buffers

	const vk::Extent3D extent = renderTarget.extent();
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		ProfilerRegion ps("create images");

		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAlbedo = make_shared<Image>(commandBuffer.mDevice, "gAlbedo", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mTonemapResult = make_shared<Image>(commandBuffer.mDevice, "gOutput", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mVisibility = make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", extent.width * extent.height * sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mDebugImage = make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);

		mCurFrame->mPathData = {
			{ "gPathStates", 		make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", 		extent.width * extent.height * sizeof(PathState), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gRayDifferentials", 	make_shared<Buffer>(commandBuffer.mDevice, "gRayDifferentials", extent.width * extent.height * sizeof(RayDifferential), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gRNGStates", 		make_shared<Buffer>(commandBuffer.mDevice, "gRNGStates", 		extent.width * extent.height * sizeof(uint4), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) }
		};
		mCurFrame->mFrameNumber = 0;
	}

	// upload views
	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size() * sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(mCurFrame->mViews.data(), views.data(), mCurFrame->mViews.size_bytes());

	// per-frame push constants
	BDPTPushConstants push_constants = mPushConstants;
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = rand();

	// check if views are inside a volume
	mCurFrame->mViewMediumIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewMediumInstances", views.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mCurFrame->mViewMediumIndices, INVALID_INSTANCE);
	bool has_volumes = false;
	mNode.for_each_descendant<Medium>([&](const component_ptr<Medium>& vol) {
		has_volumes = true;
		for (uint32_t i = 0; i < views.size(); i++) {
			const float3 view_pos = views[i].camera_to_world.transform_point(float3::Zero());
			const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point(view_pos);
			if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
				mCurFrame->mViewMediumIndices[i] = mCurFrame->mSceneData->mInstanceTransformMap.at(vol.get()).second;
		}
	});

	uint32_t sampling_flags = mTraceStepPipeline->specialization_constant("gSpecializationFlags");

	if (push_constants.gEnvironmentMaterialAddress == -1) {
		sampling_flags &= ~BDPT_FLAG_HAS_ENVIRONMENT;
		push_constants.gEnvironmentSampleProbability = 0;
	} else
		sampling_flags |= BDPT_FLAG_HAS_ENVIRONMENT;

	if (push_constants.gLightCount)
		sampling_flags |= BDPT_FLAG_HAS_EMISSIVES;
	else
		sampling_flags &= ~BDPT_FLAG_HAS_EMISSIVES;

	if (has_volumes)
		sampling_flags |= BDPT_FLAG_HAS_MEDIA;
	else
		sampling_flags &= ~BDPT_FLAG_HAS_MEDIA;

	mVisibilityPipeline->specialization_constant("gSpecializationFlags") = sampling_flags;
	mTraceStepPipeline->specialization_constant("gSpecializationFlags") = sampling_flags;

	mCurFrame->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "bdpt_view_descriptors");
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mViews : mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewMediumInstances"), mCurFrame->mViewMediumIndices);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gVisibility"), mCurFrame->mVisibility);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gDebugImage"), image_descriptor(mCurFrame->mDebugImage, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	for (const auto&[name, buf] : mCurFrame->mPathData)
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at(name), buf);

	auto bind_descriptors_and_push_constants = [&]() {
		commandBuffer.bind_descriptor_set(0, mCurFrame->mSceneDescriptors);
		commandBuffer.bind_descriptor_set(1, mCurFrame->mViewDescriptors);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(BDPTPushConstants), &push_constants);
	};

	mCurFrame->mSceneDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);

	commandBuffer.clear_color_image(mCurFrame->mRadiance, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
	commandBuffer.clear_color_image(mCurFrame->mDebugImage, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));

	{ // Visibility
		ProfilerRegion ps("Sample visibility", commandBuffer);
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mDebugImage.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mVisibilityPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	if (mPushConstants.gMaxPathVertices > 1) { // Trace paths
		ProfilerRegion ps("Trace paths", commandBuffer);
		commandBuffer.bind_pipeline(mTraceStepPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		for (uint i = 2; i+1 <= mPushConstants.gMaxPathVertices; i++) {
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			for (const auto&[name, buf] : mCurFrame->mPathData)
				commandBuffer.barrier({buf}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.dispatch_over(extent);
		}
	}

	Image::View tonemap_in = mCurFrame->mRadiance;
	if ((DebugMode)mVisibilityPipeline->specialization_constant("gDebugMode") != DebugMode::eNone) tonemap_in = mCurFrame->mDebugImage;

	if (mDenoise) {
		auto denoiser = mNode.find<Denoiser>();
		if (denoiser) {
			mCurFrame->mDenoiseResult = denoiser->denoise(commandBuffer, mCurFrame->mRadiance, mCurFrame->mViews, mCurFrame->mVisibility);
			tonemap_in = mCurFrame->mDenoiseResult;
		}
	}

	{
		ProfilerRegion ps("Tonemap", commandBuffer);
		tonemap_in.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mTonemapResult.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->specialization_constant("gModulateAlbedo") = (bool)(sampling_flags & BDPT_FLAG_DEMODULATE_ALBEDO);
		mTonemapPipeline->descriptor("gInput") = image_descriptor(tonemap_in, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mTonemapResult, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	if (mCurFrame->mTonemapResult.image()->format() == renderTarget.image()->format())
		commandBuffer.copy_image(mCurFrame->mTonemapResult, renderTarget);
	else
		commandBuffer.blit_image(mCurFrame->mTonemapResult, renderTarget);
}

}