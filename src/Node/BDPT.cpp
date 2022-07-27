#include "BDPT.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

#include <Shaders/tonemap.h>

namespace stm {

void inspector_gui_fn(Inspector& inspector, BDPT* v) { v->on_inspector_gui(); }

BDPT::BDPT(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);
	app->OnUpdate.add_listener(mNode, bind(&BDPT::update, this, std::placeholders::_1, std::placeholders::_2), Node::EventPriority::eAlmostLast);

#ifdef STRATUM_ENABLE_OPENXR
	auto xrnode = app.node().find_in_descendants<XR>();
	if (xrnode) {
		xrnode->OnRender.add_listener(mNode, [&, app](CommandBuffer& commandBuffer) {
			vector<pair<ViewData,TransformData>> views;
			views.reserve(xrnode->views().size());
			for (const XR::View& v : xrnode->views())
				views.emplace_back(v.mCamera.view(), node_to_world(v.mCamera.node()));
			render(commandBuffer, xrnode->back_buffer(), views);
			xrnode->back_buffer().transition_barrier(commandBuffer, vk::ImageLayout::eTransferSrcOptimal);
			});

		app->OnRenderWindow.add_listener(mNode, [=](CommandBuffer& commandBuffer) {
			commandBuffer.blit_image(xrnode->back_buffer(), app->window().back_buffer());
			}, Node::EventPriority::eAlmostFirst);
	} else
#endif
	{
		auto scene = mNode.find_in_ancestor<Scene>();
		app->OnRenderWindow.add_listener(mNode, [&, app, scene](CommandBuffer& commandBuffer) {
			render(commandBuffer, app->window().back_buffer(), { { scene->mMainCamera->view(), node_to_world(scene->mMainCamera.node()) } });
		});
	}

	create_pipelines();
}

void BDPT::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	float exposure = 1;
	uint32_t tonemapper = 0;
	if (mTonemapPipeline) {
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		tonemapper = mTonemapPipeline->specialization_constant<uint32_t>("gMode");
	} else {
		mPushConstants.gMinPathVertices = 3;
		mPushConstants.gMaxPathVertices = 8;
		mPushConstants.gMaxLightPathVertices = 4;
		mPushConstants.gMaxNullCollisions = 64;
		mPushConstants.gLightPathCount = 64;
		mPushConstants.gNEEReservoirM = 1;
		mPushConstants.gNEEReservoirSpatialSamples = 1;
		mPushConstants.gNEEReservoirSpatialRadius = 30;
		mPushConstants.gReservoirMaxM = 128;
		mPushConstants.gLightPresampleTileSize = 1024;
		mPushConstants.gLightPresampleTileCount = 128;
	}

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSamplerRepeat", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings[2];
	auto process_shader = [&](shared_ptr<ComputePipelineState>& dst, const fs::path& path, const string& entry_point = "", const vector<string>& compile_args = {}) {
		shared_ptr<Shader> shader;
		if (entry_point.empty()) {
			shader = make_shared<Shader>(instance->device(), path);
			dst = make_shared<ComputePipelineState>(path.stem().string(), shader);
		} else {
			shader = make_shared<Shader>(instance->device(), path, entry_point, compile_args);
			dst = make_shared<ComputePipelineState>(path.stem().string() + "_" + entry_point, shader);
		}

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

	process_shader(mSamplePhotonsPipeline, "../../src/Shaders/kernels/renderers/bdpt.hlsl", "sample_photons", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	process_shader(mSampleVisibilityPipeline, "../../src/Shaders/kernels/renderers/bdpt.hlsl", "sample_visibility", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	process_shader(mPresampleLightPipeline, "../../src/Shaders/kernels/renderers/bdpt.hlsl", "presample_lights", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	for (uint32_t i = 0; i < (uint32_t)IntegratorType::eIntegratorTypeCount; i++)
		process_shader(mIntegratorPipelines[(IntegratorType)i], "../../src/Shaders/kernels/renderers/bdpt.hlsl", to_string((IntegratorType)i), { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });

	for (uint32_t i = 0; i < 2; i++)
		mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "bdpt_descriptor_set_layout" + to_string(i), bindings[i]);

	mTonemapPipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap.spv"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;
	mTonemapPipeline->specialization_constant<uint32_t>("gMode") = tonemapper;

	mTonemapReducePipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap_reduce.spv"));

	mRayCount = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	memset(mRayCount.data(), 0, mRayCount.size_bytes());
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
}

void BDPT::on_inspector_gui() {
	if (mTonemapPipeline && ImGui::Button("Reload Shaders")) {
		mTonemapPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	Gui::enum_dropdown("BDPT Debug Mode", mDebugMode, (uint32_t)BDPTDebugMode::eDebugModeCount, [](uint32_t i) { return to_string((BDPTDebugMode)i); });

	if (mDebugMode == BDPTDebugMode::ePathLengthContribution) {
		ImGui::Indent();
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("View Path Length", ImGuiDataType_U32, &mPushConstants.gDebugViewPathLength, 1);
		ImGui::DragScalar("Light Path Length", ImGuiDataType_U32, &mPushConstants.gDebugLightPathLength, 1);
		ImGui::PopItemWidth();
		ImGui::Unindent();
	}

	ImGui::CheckboxFlags("Count Rays/second", &mSamplingFlags, BDPT_FLAG_COUNT_RAYS);
	if (mSamplingFlags & BDPT_FLAG_COUNT_RAYS) {
		const auto [rps, ext] = format_number(mRaysPerSecond);
		ImGui::Text("%.2f%s Rays/second", rps, ext);
	}

	if (ImGui::CollapsingHeader("Path Tracer")) {
		ImGui::Indent();

		Gui::enum_dropdown("Integrator", mIntegratorType, (uint32_t)IntegratorType::eIntegratorTypeCount, [](uint32_t i) { return to_string((IntegratorType)i); });

		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Remap Threads", &mSamplingFlags, BDPT_FLAG_REMAP_THREADS);
		ImGui::CheckboxFlags("Demodulate Albedo", &mSamplingFlags, BDPT_FLAG_DEMODULATE_ALBEDO);
		ImGui::CheckboxFlags("Ray Cone LoD", &mSamplingFlags, BDPT_FLAG_RAY_CONES);
		ImGui::CheckboxFlags("BSDF Sampling", &mSamplingFlags, BDPT_FLAG_SAMPLE_BSDFS);
		ImGui::CheckboxFlags("Connect Light Paths to Views", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_VIEWS);
		ImGui::CheckboxFlags("Connect Light Paths to View Paths", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_LIGHT_PATHS);
		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Light Paths")) {
		ImGui::Indent();

		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Path Vertices", ImGuiDataType_U32, &mPushConstants.gMaxPathVertices);
		ImGui::DragScalar("Min Path Vertices", ImGuiDataType_U32, &mPushConstants.gMinPathVertices);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions);
		if (mSamplingFlags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
			uint32_t mn = 1;
			ImGui::DragScalar("Light Path Count", ImGuiDataType_U32, &mPushConstants.gLightPathCount, 1, &mn);
			ImGui::DragScalar("Max Light Path Length", ImGuiDataType_U32, &mPushConstants.gMaxLightPathVertices, 1, &mn);
		}
		ImGui::PopItemWidth();

		ImGui::CheckboxFlags("NEE", &mSamplingFlags, BDPT_FLAG_NEE);
		if (mSamplingFlags & BDPT_FLAG_NEE) {
			ImGui::Indent();
			if (mPushConstants.gEnvironmentMaterialAddress != -1 && mPushConstants.gLightCount > 0) {
				ImGui::PushItemWidth(40);
				ImGui::DragFloat("Environment Sample Probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
				ImGui::PopItemWidth();
			}
			if (mSamplingFlags & BDPT_FLAG_SAMPLE_BSDFS)
				ImGui::CheckboxFlags("Multiple Importance", &mSamplingFlags, BDPT_FLAG_NEE_MIS);
			if (mPushConstants.gLightCount > 0) {
				ImGui::CheckboxFlags("Sample Light Power", &mSamplingFlags, BDPT_FLAG_SAMPLE_LIGHT_POWER);
				ImGui::CheckboxFlags("Uniform Sphere Sampling", &mSamplingFlags, BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
			}

			ImGui::CheckboxFlags("Presample Lights", &mSamplingFlags, BDPT_FLAG_PRESAMPLE_LIGHTS);
			if (mSamplingFlags & BDPT_FLAG_PRESAMPLE_LIGHTS) {
				ImGui::Indent();
				ImGui::PushItemWidth(40);
				ImGui::DragScalar("Presample Tile Size", ImGuiDataType_U32, &mPushConstants.gLightPresampleTileSize);
				if (mPushConstants.gLightPresampleTileSize == 0) mPushConstants.gLightPresampleTileSize = 1;
				ImGui::DragScalar("Presample Tile Count", ImGuiDataType_U32, &mPushConstants.gLightPresampleTileCount);
				if (mPushConstants.gLightPresampleTileCount == 0) mPushConstants.gLightPresampleTileCount = 1;
				ImGui::PopItemWidth();
				ImGui::Unindent();
			}

			ImGui::CheckboxFlags("NEE Reservoir Sampling", &mSamplingFlags, BDPT_FLAG_RESERVOIR_NEE);
			if (mSamplingFlags & BDPT_FLAG_RESERVOIR_NEE) {
				ImGui::Indent();

				ImGui::PushItemWidth(40);
				ImGui::DragScalar("Candidate Samples", ImGuiDataType_U32, &mPushConstants.gNEEReservoirM);
				if (mPushConstants.gNEEReservoirM == 0) mPushConstants.gNEEReservoirM = 1;
				ImGui::PopItemWidth();

				ImGui::CheckboxFlags("Temporal Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE);
				ImGui::CheckboxFlags("Spatial Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_SPATIAL_REUSE);

				ImGui::PushItemWidth(40);

				ImGui::Indent();
				if (mSamplingFlags & BDPT_FLAG_RESERVOIR_SPATIAL_REUSE) {
					ImGui::DragScalar("Spatial Samples", ImGuiDataType_U32, &mPushConstants.gNEEReservoirSpatialSamples);
					if (mPushConstants.gNEEReservoirSpatialSamples == 0) mPushConstants.gNEEReservoirSpatialSamples = 1;
					ImGui::DragScalar("Spatial Radius", ImGuiDataType_U32, &mPushConstants.gNEEReservoirSpatialRadius);
				}
				if (mSamplingFlags & (BDPT_FLAG_RESERVOIR_SPATIAL_REUSE|BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE)) {
					ImGui::DragScalar("Max M", ImGuiDataType_U32, &mPushConstants.gReservoirMaxM);
					if (mPushConstants.gReservoirMaxM == 0) mPushConstants.gReservoirMaxM = 1;
					ImGui::PopItemWidth();
					ImGui::CheckboxFlags("Unbiased Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_UNBIASED_REUSE);
					ImGui::PushItemWidth(40);
				}
				ImGui::Unindent();

				ImGui::PopItemWidth();
				ImGui::Unindent();
			}

			ImGui::Unindent();
		}

		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		ImGui::Indent();
		if (auto denoiser = mNode.find<Denoiser>(); denoiser)
			if (ImGui::Checkbox("Accumulation", &mDenoise))
				denoiser->reset_accumulation();
		Gui::enum_dropdown("Tone mapping", mTonemapPipeline->specialization_constant<uint32_t>("gMode"), (uint32_t)TonemapMode::eTonemapModeCount, [](uint32_t i){ return to_string((TonemapMode)i); });
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant<uint32_t>("gGammaCorrection")));
		ImGui::Unindent();
	}

	if (mPrevFrame && ImGui::CollapsingHeader("Output")) {
		ImGui::Indent();
		static char path[256]{ 'i', 'm', 'a', 'g', 'e', '.', 'h', 'd', 'r', '\0' };
		ImGui::InputText("", path, sizeof(path));
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
			ImGui::Unindent();
		}
	}
}

void BDPT::update(CommandBuffer& commandBuffer, const float deltaTime) {
	ProfilerRegion ps("BDPT::update", commandBuffer);

	Buffer::View<stm::VisibilityInfo> selectionData;
	Node* selected = nullptr;

	// reuse old frame resources
	{
		ProfilerRegion ps("Allocate Frame Resources", commandBuffer);
		if (mCurFrame) {
			mFrameResourcePool.push_front(mCurFrame);
			mPrevFrame = mCurFrame;
		}
		mCurFrame.reset();

		// reuse old frame resources
		for (auto it = mFrameResourcePool.begin(); it != mFrameResourcePool.end(); it++) {
			if (*it != mPrevFrame && (*it)->mFence->status() == vk::Result::eSuccess) {
				mCurFrame = *it;
				selectionData = (*it)->mSelectionData;
				if (selectionData && selectionData.data()->instance_index() != INVALID_INSTANCE)
					selected = (*it)->mSceneData->mInstanceNodes[selectionData.data()->instance_index()];
				mFrameResourcePool.erase(it);
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

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mRayCount[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mRayCount[0];
		mRaysPerSecondTimer = 0;
	}

	mPushConstants.gEnvironmentMaterialAddress = mCurFrame->mSceneData->mEnvironmentMaterialAddress;
	mPushConstants.gLightCount = (uint32_t)mCurFrame->mSceneData->mLightInstances.size();
	mPushConstants.gLightDistributionPDF = mCurFrame->mSceneData->mLightDistributionPDF;
	mPushConstants.gLightDistributionCDF = mCurFrame->mSceneData->mLightDistributionCDF;

	if (!mCurFrame->mSceneDescriptors) mCurFrame->mSceneDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[0], "path_tracer_scene_descriptors");
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gScene"), **mCurFrame->mSceneData->mScene);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVertices"), mCurFrame->mSceneData->mVertices);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gIndices"), mCurFrame->mSceneData->mIndices);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstances"), mCurFrame->mSceneData->mInstances);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceTransforms"), mCurFrame->mSceneData->mInstanceTransforms);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceInverseTransforms"), mCurFrame->mSceneData->mInstanceInverseTransforms);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceMotionTransforms"), mCurFrame->mSceneData->mInstanceMotionTransforms);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gMaterialData"), mCurFrame->mSceneData->mMaterialData);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gDistributions"), mCurFrame->mSceneData->mDistributionData);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gLightInstances"), mCurFrame->mSceneData->mLightInstances);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gRayCount"), mRayCount);
	for (const auto& [image, index] : mCurFrame->mSceneData->mResources.images)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto& [vol, index] : mCurFrame->mSceneData->mResources.volume_data_map)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVolumes"), index, vol);
	mCurFrame->mSceneDescriptors->flush_writes();
	mCurFrame->mSceneDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);

	if (selectionData && !ImGui::GetIO().WantCaptureMouse) {
		if (commandBuffer.mDevice.mInstance.window().input_state().pressed(KeyCode::eMouse1) && !commandBuffer.mDevice.mInstance.window().input_state_last().pressed(KeyCode::eMouse1)) {
			component_ptr<Inspector> inspector = mNode.node_graph().find_components<Inspector>().front();
			inspector->select(mNode.node_graph().contains(selected) ? selected : nullptr);
		}
	}
}

void BDPT::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views) {
	if (!mCurFrame || !mCurFrame->mSceneData) {
		commandBuffer.clear_color_image(renderTarget, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
		return;
	}

	ProfilerRegion ps("BDPT::render", commandBuffer);

	// Initialize buffers

	const vk::Extent3D extent = renderTarget.extent();
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		ProfilerRegion ps("create images");

		mCurFrame->mRadiance 		= make_shared<Image>(commandBuffer.mDevice, "gRadiance",   extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAlbedo 			= make_shared<Image>(commandBuffer.mDevice, "gAlbedo",     extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mPrevUVs 		= make_shared<Image>(commandBuffer.mDevice, "gPrevUVs",    extent, vk::Format::eR32G32Sfloat,       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mDebugImage 		= make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mTonemapResult 	= make_shared<Image>(commandBuffer.mDevice, "gOutput",     extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

		mCurFrame->mTonemapMax = make_shared<Buffer>(commandBuffer.mDevice, "gMax", sizeof(uint4), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY),
		mCurFrame->mSelectionData = make_shared<Buffer>(commandBuffer.mDevice, "gSelectionData", sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU),
		mCurFrame->mPathData = {
			{ "gVisibility", 		 make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", 		   extent.width * extent.height * sizeof(VisibilityInfo), 													vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gPathStates", 		 make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", 		   extent.width * extent.height * sizeof(PathState), 														vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gRayDifferentials", 	 make_shared<Buffer>(commandBuffer.mDevice, "gRayDifferentials",   extent.width * extent.height * sizeof(RayDifferential), 													vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gReservoirs", 		 make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", 		   extent.width * extent.height * sizeof(Reservoir), 														vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gReservoirSamples",	 make_shared<Buffer>(commandBuffer.mDevice, "gReservoirSamples",   extent.width * extent.height * sizeof(uint4), 	    													vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gLightTraceSamples",  make_shared<Buffer>(commandBuffer.mDevice, "gLightTraceSamples",  extent.width * extent.height * sizeof(float4), 															vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gLightPathVertices0", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices0", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex0), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gLightPathVertices1", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices1", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex1), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gLightPathVertices2", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices2", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex2), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY) },
			{ "gLightPathVertices3", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices3", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex3), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY) },
		};
		mCurFrame->mFrameNumber = 0;
	}

	if (mCurFrame->mPathData.at("gLightPathVertices0").size_bytes() < extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex0)) {
		mCurFrame->mPathData["gLightPathVertices0"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices0", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex0), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices1"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices1", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex1), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices2"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices2", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex2), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices3"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices3", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex3), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
	}

	if (!mCurFrame->mPresampledLights || mCurFrame->mPresampledLights.size() < max(1u,mPushConstants.gLightPresampleTileCount*mPushConstants.gLightPresampleTileSize))
		mCurFrame->mPresampledLights = make_shared<Buffer>(commandBuffer.mDevice, "gPresampledLights", max(1u,mPushConstants.gLightPresampleTileCount*mPushConstants.gLightPresampleTileSize) * sizeof(PresampledLightPoint), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

	// upload views
	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size() * sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	mCurFrame->mViewTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	mCurFrame->mViewInverseTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewInverseTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	for (uint32_t i = 0; i < views.size(); i++) {
		mCurFrame->mViews[i] = views[i].first;
		mCurFrame->mViewTransforms[i] = views[i].second;
		mCurFrame->mViewInverseTransforms[i] = views[i].second.inverse();
	}

	// per-frame push constants
	BDPTPushConstants push_constants = mPushConstants;
	push_constants.gOutputExtent = uint2(extent.width, extent.height);
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = rand();

	// check if views are inside a volume
	mCurFrame->mViewMediumIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewMediumInstances", views.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mCurFrame->mViewMediumIndices, INVALID_INSTANCE);
	bool has_volumes = false;
	mNode.for_each_descendant<Medium>([&](const component_ptr<Medium>& vol) {
		has_volumes = true;
		for (uint32_t i = 0; i < views.size(); i++) {
			const float3 view_pos = mCurFrame->mViewTransforms[i].transform_point(float3::Zero());
			const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point(view_pos);
			if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
				mCurFrame->mViewMediumIndices[i] = mCurFrame->mSceneData->mInstanceTransformMap.at(vol.get()).second;
		}
	});

	uint32_t sampling_flags = mSamplingFlags;
	{
		if (push_constants.gEnvironmentMaterialAddress == -1) {
			sampling_flags &= ~BDPT_FLAG_HAS_ENVIRONMENT;
			push_constants.gEnvironmentSampleProbability = 0;
		} else
			sampling_flags |= BDPT_FLAG_HAS_ENVIRONMENT;

		if (push_constants.gLightCount)
			sampling_flags |= BDPT_FLAG_HAS_EMISSIVES;
		else {
			sampling_flags &= ~BDPT_FLAG_HAS_EMISSIVES;
			push_constants.gEnvironmentSampleProbability = 1;
		}

		if (has_volumes)
			sampling_flags |= BDPT_FLAG_HAS_MEDIA;
		else {
			sampling_flags &= ~BDPT_FLAG_HAS_MEDIA;
			push_constants.gMaxNullCollisions = 0;
		}

		if (push_constants.gLightCount == 0 && push_constants.gEnvironmentMaterialAddress == -1)
			sampling_flags &= ~BDPT_FLAG_NEE;

		if (!(sampling_flags & BDPT_FLAG_NEE))
			sampling_flags &= ~BDPT_FLAG_PRESAMPLE_LIGHTS;

		if (!mPrevFrame || !mPrevFrame->mPathData.contains("gReservoirs") || !mPrevFrame->mPathData.at("gReservoirs"))
			sampling_flags &= ~(BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE);

		mPresampleLightPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags | BDPT_FLAG_UNIFORM_SPHERE_SAMPLING;
		mPresampleLightPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;

		mSamplePhotonsPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = (sampling_flags | BDPT_FLAG_TRACE_LIGHT | BDPT_FLAG_UNIFORM_SPHERE_SAMPLING) & ~BDPT_FLAG_RAY_CONES;
		mSamplePhotonsPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;

		mSampleVisibilityPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		mSampleVisibilityPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;

		for (uint32_t i = 0; i < (uint32_t)IntegratorType::eIntegratorTypeCount; i++) {
			mIntegratorPipelines[(IntegratorType)i]->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
			mIntegratorPipelines[(IntegratorType)i]->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
		}
	}

	// set descriptors
	{
		mCurFrame->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "bdpt_view_descriptors");
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mCurFrame->mViews);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewTransforms"), mCurFrame->mViewTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gInverseViewTransforms"), mCurFrame->mViewInverseTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mViews : mCurFrame->mViews);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevInverseViewTransforms"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mViewInverseTransforms : mCurFrame->mViewInverseTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevVisibility"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mPathData.at("gVisibility") : mCurFrame->mPathData.at("gVisibility"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewMediumInstances"), mCurFrame->mViewMediumIndices);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevUVs"), image_descriptor(mCurFrame->mPrevUVs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gDebugImage"), image_descriptor(mCurFrame->mDebugImage, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevReservoirs"), (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) ? mPrevFrame->mPathData.at("gReservoirs") : mCurFrame->mPathData.at("gReservoirs"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevReservoirSamples"), (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) ? mPrevFrame->mPathData.at("gReservoirSamples") : mCurFrame->mPathData.at("gReservoirSamples"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPresampledLights"), mCurFrame->mPresampledLights);
		for (const auto&[name, buf] : mCurFrame->mPathData)
			mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at(name), buf);
	}

	auto bind_descriptors_and_push_constants = [&]() {
		commandBuffer.bind_descriptor_set(0, mCurFrame->mSceneDescriptors);
		commandBuffer.bind_descriptor_set(1, mCurFrame->mViewDescriptors);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(BDPTPushConstants), &push_constants);
	};

	commandBuffer.clear_color_image(mCurFrame->mRadiance, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
	commandBuffer.clear_color_image(mCurFrame->mDebugImage, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
	mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mCurFrame->mPrevUVs.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mCurFrame->mDebugImage.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

	// presample lights
	if (push_constants.gMaxPathVertices > 2 && (sampling_flags & BDPT_FLAG_PRESAMPLE_LIGHTS)) {
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Presample_lights");
		ProfilerRegion ps("Presample lights", commandBuffer);
		commandBuffer.bind_pipeline(mPresampleLightPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(mPushConstants.gLightPresampleTileSize * mPushConstants.gLightPresampleTileCount);
	}

	// trace light paths
	if (sampling_flags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
		auto lv2 = mCurFrame->mPathData.at("gLightPathVertices2");
		auto lt = mCurFrame->mPathData.at("gLightTraceSamples");
		commandBuffer->fillBuffer(**lt.buffer(), lt.offset(), lt.size_bytes(), 0);
		commandBuffer->fillBuffer(**lv2.buffer(), lv2.offset(), lv2.size_bytes(), 0);
		commandBuffer.barrier({ lv2, lt }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		vk::Extent3D lightExtent;
		const uint32_t n = (mPushConstants.gLightPathCount + 31) / 32;
		lightExtent.height = (uint32_t)sqrt((float)n);
		lightExtent.width = (n + (lightExtent.height-1)) / lightExtent.height;
		lightExtent.depth = 1;

		{
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Sample photons");
			ProfilerRegion ps("Sample photons", commandBuffer);
			commandBuffer.bind_pipeline(mSamplePhotonsPipeline->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
		}

		if (push_constants.gMaxLightPathVertices > 2) {
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace light paths");
			ProfilerRegion ps("Trace light paths", commandBuffer);
			mIntegratorPipelines[mIntegratorType]->specialization_constant<uint32_t>("gSpecializationFlags") = (sampling_flags | BDPT_FLAG_TRACE_LIGHT) & ~BDPT_FLAG_RAY_CONES;
			commandBuffer.bind_pipeline(mIntegratorPipelines[mIntegratorType]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			const uint32_t n = mIntegratorType == IntegratorType::eMultiKernel ? push_constants.gMaxLightPathVertices + 1 : 3;
			for (uint i = 2; i < n; i++) {
				commandBuffer.barrier({
					mCurFrame->mPathData["gPathStates"]
				}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.dispatch_over(extent);
			}
		}

		for (const auto&[name, buf] : mCurFrame->mPathData)
			commandBuffer.barrier({buf}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		mIntegratorPipelines[mIntegratorType]->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		push_constants.gOutputExtent = uint2(extent.width, extent.height);
	}

	// trace visibility
	{
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Sample visibility");
		ProfilerRegion ps("Sample visibility", commandBuffer);
		commandBuffer.bind_pipeline(mSampleVisibilityPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	if (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) {
		commandBuffer.barrier({
			mPrevFrame->mPathData.at("gVisibility"),
			mPrevFrame->mPathData.at("gReservoirs"),
			mPrevFrame->mPathData.at("gReservoirSamples"),
		}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}
	if (sampling_flags & BDPT_FLAG_PRESAMPLE_LIGHTS)
		commandBuffer.barrier({ mCurFrame->mPresampledLights }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	mCurFrame->mPrevUVs.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

	// trace view paths
	if (push_constants.gMaxPathVertices > 2) {
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace view paths");
		ProfilerRegion ps("Trace view paths", commandBuffer);
		commandBuffer.bind_pipeline(mIntegratorPipelines[mIntegratorType]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		const uint32_t n = mIntegratorType == IntegratorType::eMultiKernel ? push_constants.gMaxPathVertices + 1 : 3;
		for (uint i = 2; i < n; i++) {
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({
				mCurFrame->mPathData["gPathStates"],
				mCurFrame->mPathData["gRayDifferentials"],
			}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.dispatch_over(extent);
		}
	}

	Image::View result = mCurFrame->mRadiance;
	if (mDebugMode != BDPTDebugMode::eNone)
		result = mCurFrame->mDebugImage;

	component_ptr<Denoiser> denoiser = mNode.find<Denoiser>();
	const bool reprojection = denoiser ? denoiser->reprojection() : false;
	const bool changed = mPrevFrame && mPrevFrame->mViewTransforms && (mCurFrame->mViewTransforms[0].m != mPrevFrame->mViewTransforms[0].m).any();

	if (changed && (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) && !reprojection) {
		// reset reservoir history
		commandBuffer.barrier({mCurFrame->mPathData.at("gReservoirs")}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
		commandBuffer->fillBuffer(**mCurFrame->mPathData.at("gReservoirs").buffer(), mCurFrame->mPathData.at("gReservoirs").offset(), mCurFrame->mPathData.at("gReservoirs").size_bytes(), 0);
	}

	if (mDenoise && denoiser) {
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Denoise");
		if (changed && !reprojection) denoiser->reset_accumulation();
		mCurFrame->mDenoiseResult = denoiser->denoise(commandBuffer, result, mCurFrame->mViews, mCurFrame->mPathData.at("gVisibility").cast<VisibilityInfo>(), mCurFrame->mPrevUVs);
		result = mCurFrame->mDenoiseResult;
	}

	// tone map
	{
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Tonemap");
		ProfilerRegion ps("Tonemap", commandBuffer);

		if (gTonemapModeNeedsMax.contains((TonemapMode)mTonemapPipeline->specialization_constant<uint32_t>("gMode"))) {
			ProfilerRegion ps("Tonemap Reduce", commandBuffer);
			commandBuffer.barrier({mCurFrame->mTonemapMax}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite,  vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
			commandBuffer->fillBuffer(**mCurFrame->mTonemapMax.buffer(), mCurFrame->mTonemapMax.offset(), mCurFrame->mTonemapMax.size_bytes(), 0);
			commandBuffer.barrier({mCurFrame->mTonemapMax}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			mTonemapReducePipeline->specialization_constant<uint32_t>("gModulateAlbedo") = (bool)(sampling_flags & BDPT_FLAG_DEMODULATE_ALBEDO);
			mTonemapReducePipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapReducePipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapReducePipeline->descriptor("gMax") = mCurFrame->mTonemapMax;
			commandBuffer.bind_pipeline(mTonemapReducePipeline->get_pipeline());
			mTonemapReducePipeline->bind_descriptor_sets(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mTonemapResult.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.barrier({mCurFrame->mTonemapMax}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite,  vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		mTonemapPipeline->specialization_constant<uint32_t>("gModulateAlbedo") = (bool)(sampling_flags & BDPT_FLAG_DEMODULATE_ALBEDO);
		mTonemapPipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mTonemapResult, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gMax") = mCurFrame->mTonemapMax;
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	if (mCurFrame->mTonemapResult.image()->format() == renderTarget.image()->format())
		commandBuffer.copy_image(mCurFrame->mTonemapResult, renderTarget);
	else
		commandBuffer.blit_image(mCurFrame->mTonemapResult, renderTarget);

	// copy selection data
	{
		Buffer::View<VisibilityInfo> v = mCurFrame->mPathData.at("gVisibility").cast<VisibilityInfo>();
		commandBuffer.barrier({ v }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		const float2 c = commandBuffer.mDevice.mInstance.window().input_state().cursor_pos();
		const int2 cp = int2((int)c.x(), (int)c.y());
		for (const auto&[view, transform] : views)
			if (view.test_inside(cp))
				commandBuffer.copy_buffer(Buffer::View<VisibilityInfo>(v, cp.y() * view.extent().x() + cp.x(), 1), mCurFrame->mSelectionData);
	}

	commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eTopOfPipe, "BDPT::render done");
}

}