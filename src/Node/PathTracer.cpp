#include "PathTracer.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

#include <HLSL/tonemap.h>

namespace stm {

void inspector_gui_fn(PathTracer* v) { v->on_inspector_gui(); }

PathTracer::PathTracer(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);
  	app->OnUpdate.listen(mNode, bind(&PathTracer::update, this, std::placeholders::_1, std::placeholders::_2), EventPriority::eAlmostLast);

#ifdef STRATUM_ENABLE_OPENXR
	auto xrnode = app.node().find_in_descendants<XR>();
	if (xrnode) {
		xrnode->OnRender.listen(mNode, [&,app](CommandBuffer& commandBuffer) {
			vector<ViewData> views;
			views.reserve(xrnode->views().size());
			for (const XR::View& v : xrnode->views())
				views.emplace_back( v.mCamera.view(node_to_world(v.mCamera.node())) );
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
		app->OnRenderWindow.listen(mNode, [&,app,scene](CommandBuffer& commandBuffer) {
			render(commandBuffer, app->window().back_buffer(), { scene->mMainCamera->view(node_to_world(scene->mMainCamera.node())) });
		});
	}

	create_pipelines();
}

void PathTracer::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	uint32_t debugMode;
	float exposure;
	optional<uint32_t> samplingFlags;
	if (mRandomWalkPipeline) {
		// pipelines already exist, keep track of old inputs
		debugMode = mSampleVisibilityPipeline->specialization_constant("gDebugMode");
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		samplingFlags = mRandomWalkPipeline->specialization_constant("gSamplingFlags");
	} else {
		debugMode = 0;
		exposure = 1;
		mPushConstants.gMaxEyeDepth = 8;
		mPushConstants.gMaxLightDepth = 0;
		mPushConstants.gMinDepth = 1;
		mPushConstants.gReservoirSamples = 1;
		mPushConstants.gMaxNullCollisions = 64;
	}

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	auto samplerClamp = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
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
			if (name == "gVolumes" || name == "gImages" || name == "g3DImages") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
			b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
			bindings[binding.mSet].emplace(binding.mBinding, b);
			mDescriptorMap[binding.mSet].emplace(name, binding.mBinding);
		}
	};
	process_shader(mSamplePhotonsPipeline, "pt_sample_photons");
	process_shader(mSampleVisibilityPipeline, "pt_sample_visibility");
	process_shader(mRandomWalkPipeline, "pt_random_walk");
	process_shader(mResolvePipeline, "pt_resolve");
	for (uint32_t i = 0; i < 2; i++)
		mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "path_trace_descriptor_set_layout" + to_string(i), bindings[i]);

	if (samplingFlags) mRandomWalkPipeline->specialization_constant("gSamplingFlags") = *samplingFlags;

	mTonemapPipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap.spv"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;
	mSampleVisibilityPipeline->specialization_constant("gDebugMode") = debugMode;

	mCounterValues = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	memset(mCounterValues.data(), 0, mCounterValues.size_bytes());
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
}

void PathTracer::on_inspector_gui() {
	if (mRandomWalkPipeline && ImGui::Button("Reload Shaders")) {
		mRandomWalkPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	const auto [rps, ext] = format_number(mRaysPerSecond);
	ImGui::Text("%.2f%s Rays/second", rps, ext);

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Eye Depth", ImGuiDataType_U32, &mPushConstants.gMaxEyeDepth, 1);
		ImGui::DragScalar("Min Depth", ImGuiDataType_U32, &mPushConstants.gMinDepth, 1);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions, 1);
		ImGui::PopItemWidth();

		ImGui::CheckboxFlags("Sample Pixel Area", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_PIXEL_AREA);
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Sample Emissives", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_EMISSIVE);
		ImGui::CheckboxFlags("Sample Environment", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_ENVIRONMENT);
		if (mPushConstants.gEnvironmentMaterialAddress != -1 && mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_ENVIRONMENT) {
			ImGui::PushItemWidth(40);
			ImGui::Indent();
			ImGui::DragFloat("Environment Sample Probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
			ImGui::Unindent();
			ImGui::PopItemWidth();
		}
		if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & (SAMPLE_FLAG_SAMPLE_EMISSIVE | SAMPLE_FLAG_SAMPLE_ENVIRONMENT)) {
			ImGui::Indent();
			ImGui::CheckboxFlags("Multiple Importance", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_MIS);
			if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_EMISSIVE)
				ImGui::CheckboxFlags("Uniform Sphere Sampling", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING);

			ImGui::PushItemWidth(40);
			ImGui::DragScalar("Max Light Path Length", ImGuiDataType_U32, &mPushConstants.gMaxLightDepth, 1);
			ImGui::PopItemWidth();
			if (mPushConstants.gMaxLightDepth > 0)
				ImGui::CheckboxFlags("Randomly Sample Light Paths", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_RANDOM_LIGHT_PATHS);
			ImGui::CheckboxFlags("Reservoir Sampling", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_RESERVOIRS);
			if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_RESERVOIRS) {
				ImGui::Indent();
				ImGui::PushItemWidth(40);
				uint32_t one = 1;
				ImGui::DragScalar("Reservoir Samples", ImGuiDataType_U32, &mPushConstants.gReservoirSamples, 1, &one);
				ImGui::PopItemWidth();
				ImGui::Unindent();
			}
			ImGui::Unindent();
		}
		ImGui::CheckboxFlags("Ray Cone LoD", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_RAY_CONE_LOD);
		ImGui::CheckboxFlags("Demodulate Albedo", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_DEMODULATE_ALBEDO);
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

	uint32_t m = mSampleVisibilityPipeline->specialization_constant("gDebugMode");
	if (ImGui::BeginCombo("Debug Mode", to_string((DebugMode)m).c_str())) {
		for (uint32_t i = 0; i < (uint32_t)DebugMode::eDebugModeCount; i++)
			if (ImGui::Selectable(to_string((DebugMode)i).c_str(), m == i)) {
				mSampleVisibilityPipeline->specialization_constant("gDebugMode") = i;
			}
		ImGui::EndCombo();
	}
}

void PathTracer::update(CommandBuffer& commandBuffer, const float deltaTime) {
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
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gMaterialData"), mCurFrame->mSceneData->mMaterialData);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gDistributions"), mCurFrame->mSceneData->mDistributionData);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gLightInstances"), mCurFrame->mSceneData->mLightInstances);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gCounters"), mCounterValues);
	for (const auto& [image, index] : mCurFrame->mSceneData->mResources.images)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto& [vol, index] : mCurFrame->mSceneData->mResources.volume_data_map)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVolumes"), index, vol);
	mCurFrame->mSceneDescriptors->flush_writes();

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mCounterValues[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mCounterValues[0];
		mRaysPerSecondTimer = 0;
	}
}

void PathTracer::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<ViewData>& views) {
	if (!mCurFrame || !mCurFrame->mSceneData) {
		commandBuffer.clear_color_image(renderTarget, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
		return;
	}

	ProfilerRegion ps("PathTracer::render", commandBuffer);

	// Initialize buffers

	const vk::Extent3D extent = renderTarget.extent();
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		ProfilerRegion ps("create images");

		mCurFrame->mRadiance 				= make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAlbedo 					= make_shared<Image>(commandBuffer.mDevice, "gAlbedo", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mTonemapResult 			= make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mVisibility 				= make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", 			extent.width * extent.height * sizeof(VisibilityInfo)	, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mRadianceMutex 			= make_shared<Buffer>(commandBuffer.mDevice, "gRadianceMutex", 			extent.width * extent.height * sizeof(uint32_t)			, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathStates 				= make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", 			extent.width * extent.height * sizeof(PathState)		, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathStateVertices 		= make_shared<Buffer>(commandBuffer.mDevice, "gPathStateVertices", 		extent.width * extent.height * sizeof(PathVertex)		, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathStateShadingData 	= make_shared<Buffer>(commandBuffer.mDevice, "gPathStateShadingData", 	extent.width * extent.height * sizeof(ShadingData)		, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mReservoirs 				= make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", 			extent.width * extent.height * sizeof(Reservoir)		, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);

		mCurFrame->mFrameNumber = 0;
	}

	const size_t max_light_vertices = extent.width * extent.height * max(1u, mPushConstants.gMaxLightDepth);
	if (!mCurFrame->mLightPathVertices || mCurFrame->mLightPathVertices.size() < max_light_vertices) {
		ProfilerRegion ps("create light path buffers");
		mCurFrame->mLightPathVertices = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices", max_light_vertices * sizeof(PathVertex), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mLightPathShadingData = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathShadingData", max_light_vertices * sizeof(ShadingData), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
	}

	const bool hasHistory = mPrevFrame && mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	// upload views
	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size() * sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(mCurFrame->mViews.data(), views.data(), mCurFrame->mViews.size_bytes());

	// per-frame push constants
	PathTracePushConstants push_constants = mPushConstants;
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = rand();

	// check if views are inside a volume
	mCurFrame->mViewVolumeIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewVolumeInstances", views.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mCurFrame->mViewVolumeIndices, INVALID_INSTANCE);
	bool has_volumes = false;
	mNode.for_each_descendant<HeterogeneousVolume>([&](const component_ptr<HeterogeneousVolume>& vol) {
		has_volumes = true;
		for (uint32_t i = 0; i < views.size(); i++) {
			const float3 view_pos = views[i].camera_to_world.transform_point(float3::Zero());
			const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point(view_pos);
			if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
				mCurFrame->mViewVolumeIndices[i] = mCurFrame->mSceneData->mInstanceTransformMap.at(vol.get()).second;
		}
	});

	uint32_t sampling_flags = mRandomWalkPipeline->specialization_constant("gSamplingFlags");
	if (push_constants.gEnvironmentSampleProbability == 0 || push_constants.gEnvironmentMaterialAddress == INVALID_MATERIAL) {
		sampling_flags &= ~SAMPLE_FLAG_SAMPLE_ENVIRONMENT;
		push_constants.gEnvironmentSampleProbability = 0;
	}
	if (push_constants.gLightCount == 0)
		sampling_flags &= ~SAMPLE_FLAG_SAMPLE_EMISSIVE;
	if (has_volumes && push_constants.gMaxNullCollisions > 0)
		sampling_flags |= SAMPLE_FLAG_ENABLE_VOLUMES;
	else
		sampling_flags &= ~SAMPLE_FLAG_ENABLE_VOLUMES;
	if (push_constants.gMaxLightDepth > 0)
		sampling_flags |= SAMPLE_FLAG_SAMPLE_LIGHT_PATHS;
	else {
		sampling_flags &= ~SAMPLE_FLAG_SAMPLE_LIGHT_PATHS;
		sampling_flags &= ~SAMPLE_FLAG_RANDOM_LIGHT_PATHS;
	}
	mSamplePhotonsPipeline->specialization_constant("gSamplingFlags") = sampling_flags | SAMPLE_FLAG_TRACE_LIGHT_PATHS | SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING;
	mSampleVisibilityPipeline->specialization_constant("gSamplingFlags") = sampling_flags;
	mRandomWalkPipeline->specialization_constant("gSamplingFlags") = sampling_flags;
	mResolvePipeline->specialization_constant("gSamplingFlags") = sampling_flags;
	mTonemapPipeline->specialization_constant("gModulateAlbedo") = (bool)(sampling_flags & SAMPLE_FLAG_DEMODULATE_ALBEDO);

	mCurFrame->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "path_tracer_view_descriptors");
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPathStates"), mCurFrame->mPathStates);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPathStateVertices"), mCurFrame->mPathStateVertices);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPathStateShadingData"), mCurFrame->mPathStateShadingData);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gLightPathVertices"), mCurFrame->mLightPathVertices);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gLightPathShadingData"), mCurFrame->mLightPathShadingData);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), hasHistory ? mPrevFrame->mViews : mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewVolumeInstances"), mCurFrame->mViewVolumeIndices);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadianceMutex"), mCurFrame->mRadianceMutex);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gReservoirs"), mCurFrame->mReservoirs);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gVisibility"), mCurFrame->mVisibility);

	auto bind_descriptors_and_push_constants = [&]() {
		commandBuffer.bind_descriptor_set(0, mCurFrame->mSceneDescriptors);
		commandBuffer.bind_descriptor_set(1, mCurFrame->mViewDescriptors);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &push_constants);
	};

	commandBuffer.clear_color_image(mCurFrame->mRadiance, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));

	if (mPushConstants.gMaxLightDepth > 0) { // Light paths
		mPushConstants.gNumLightPaths = extent.width * extent.height;
		commandBuffer->fillBuffer(**mCurFrame->mRadianceMutex.buffer(), mCurFrame->mRadianceMutex.offset(), mCurFrame->mRadianceMutex.size_bytes(), 0);

		{
			ProfilerRegion ps("Sample photons", commandBuffer);
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.bind_pipeline(mSamplePhotonsPipeline->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
		}
		{
			ProfilerRegion ps("Trace light paths", commandBuffer);
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mRadianceMutex }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			mRandomWalkPipeline->specialization_constant("gSamplingFlags") = mSamplePhotonsPipeline->specialization_constant("gSamplingFlags");
			commandBuffer.bind_pipeline(mRandomWalkPipeline->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
			mRandomWalkPipeline->specialization_constant("gSamplingFlags") = sampling_flags;
		}
	}

	{ // Visibility
		ProfilerRegion ps("Sample visibility", commandBuffer);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mSceneDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		mCurFrame->mViewDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		commandBuffer.bind_pipeline(mSampleVisibilityPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	if (mPushConstants.gMaxEyeDepth > 2) { // Eye paths
		ProfilerRegion ps("Trace Eye Paths", commandBuffer);
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		if (sampling_flags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
			commandBuffer.barrier({ mCurFrame->mLightPathVertices, mCurFrame->mLightPathShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		if (sampling_flags & SAMPLE_FLAG_SAMPLE_RESERVOIRS)
			commandBuffer.barrier({ mCurFrame->mReservoirs }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mRandomWalkPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	{
		ProfilerRegion ps("Resolve Samples", commandBuffer);
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mResolvePipeline->get_pipeline(mDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(1, mCurFrame->mViewDescriptors);
		mResolvePipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	Image::View tonemap_in = mCurFrame->mRadiance;

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
		mTonemapPipeline->descriptor("gInput")  = image_descriptor(tonemap_in, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
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