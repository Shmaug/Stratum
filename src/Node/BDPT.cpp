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

	create_pipelines();
}

void BDPT::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	float exposure = 0;
	float exposure_alpha = 0.1f;
	uint32_t tonemapper = 0;
	if (mTonemapPipeline) {
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		exposure_alpha = mTonemapPipeline->push_constant<float>("gExposureAlpha");
		tonemapper = mTonemapPipeline->specialization_constant<uint32_t>("gMode");
	} else {
		mPushConstants.gMinPathVertices = 4;
		mPushConstants.gMaxPathVertices = 8;
		mPushConstants.gMaxNullCollisions = 64;
		mPushConstants.gLightPathCount = 64;
		mPushConstants.gNEEReservoirM = 1;
		mPushConstants.gNEEReservoirSpatialSamples = 1;
		mPushConstants.gNEEReservoirSpatialRadius = 30;
		mPushConstants.gReservoirMaxM = 128;
		mPushConstants.gLightPresampleTileSize = 1024;
		mPushConstants.gLightPresampleTileCount = 128;
		mPushConstants.gEnvironmentSampleProbability = 0.5f;
	}

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSamplerRepeat", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	{
		vector<tuple<shared_ptr<Shader>, string, shared_ptr<ComputePipelineState>*>> shaders;
		vector<thread> threads;
		shaders.reserve(5);
		threads.reserve(5);

		auto process_shader = [&](shared_ptr<ComputePipelineState>& dst, const fs::path& path, const string& entry_point = "", const vector<string>& compile_args = {}) {
			auto&[shader, name, pipeline] = shaders.emplace_back();
			name = entry_point.empty() ? path.stem().string() : (path.stem().string() + "_" + entry_point);
			pipeline = &dst;
			uint32_t i = shaders.size() - 1;
			threads.emplace_back([&,i,path,entry_point,compile_args]() {
				get<shared_ptr<Shader>>(shaders[i]) = make_shared<Shader>(instance->device(), path, entry_point, compile_args);
			});
		};

		const fs::path& src_path = "../../src/Shaders/kernels/renderers/bdpt.hlsl";
		const vector<string>& args = { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" };
		process_shader(mRenderPipelines[eSamplePhotons]     , src_path, "sample_photons"      , args);
		process_shader(mRenderPipelines[eSampleVisibility]  , src_path, "sample_visibility"   , args);
		process_shader(mRenderPipelines[ePresampleLights]   , src_path, "presample_lights"    , args);
		process_shader(mRenderPipelines[eTraceNEE]          , src_path, "trace_nee"           , args);
		process_shader(mRenderPipelines[ePathTraceLoop]     , src_path, "path_trace_loop"     , args);
		process_shader(mRenderPipelines[eConnect]           , src_path, "connect"             , args);
		process_shader(mRenderPipelines[eAddLightTrace]     , src_path, "add_light_trace"     , args);

		for (thread& t : threads)
			if (t.joinable())
				t.join();

		unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings[2];
		for (auto&[shader, name, pipeline] : shaders) {
			*pipeline = make_shared<ComputePipelineState>(name, shader);
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
				if (name == "gVolumes" || name == "gImages" || name == "gImage1s") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
				b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
				bindings[binding.mSet].emplace(binding.mBinding, b);
				mDescriptorMap[binding.mSet].emplace(name, binding.mBinding);
			}
		}
		for (uint32_t i = 0; i < 2; i++)
			mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "bdpt_descriptor_set_layout" + to_string(i), bindings[i]);

	}

	mTonemapPipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap.spv"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;
	mTonemapPipeline->push_constant<float>("gExposureAlpha") = exposure_alpha;
	mTonemapPipeline->specialization_constant<uint32_t>("gMode") = tonemapper;

	mTonemapMaxReducePipeline = make_shared<ComputePipelineState>("tonemap reduce", make_shared<Shader>(instance->device(), "Shaders/tonemap_reduce_max.spv"));

	mRayCount = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	mRayCount[0] = 0;
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
}

void BDPT::on_inspector_gui() {
	if (mTonemapPipeline && ImGui::Button("Reload BDPT Shaders")) {
		mTonemapPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	ImGui::SetNextItemWidth(200);
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

	ImGui::Checkbox("Pause Rendering", &mPauseRendering);
	ImGui::Checkbox("Half Precision", &mHalfColorPrecision);
	ImGui::CheckboxFlags("Remap Threads", &mSamplingFlags, BDPT_FLAG_REMAP_THREADS);

	if (ImGui::CollapsingHeader("Scene")) {
		ImGui::Indent();
		ImGui::CheckboxFlags("Flip Triangle UVs", &mSamplingFlags, BDPT_FLAG_FLIP_TRIANGLE_UVS);
		ImGui::CheckboxFlags("Flip Normal Maps", &mSamplingFlags, BDPT_FLAG_FLIP_NORMAL_MAPS);
		ImGui::CheckboxFlags("Alpha Test", &mSamplingFlags, BDPT_FLAG_ALPHA_TEST);
		ImGui::CheckboxFlags("Normal Maps", &mSamplingFlags, BDPT_FLAG_NORMAL_MAPS);
		ImGui::CheckboxFlags("Ray Cone LoD", &mSamplingFlags, BDPT_FLAG_RAY_CONES);
		ImGui::Checkbox("Force Lambertian", &mForceLambertian);
		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::Indent();
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Bounces per Dispatch", ImGuiDataType_U32, &mPathTraceKernelIterations);
		ImGui::DragScalar("Max Path Vertices", ImGuiDataType_U32, &mPushConstants.gMaxPathVertices);
		ImGui::DragScalar("Min Path Vertices", ImGuiDataType_U32, &mPushConstants.gMinPathVertices);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Coherent RR", &mSamplingFlags, BDPT_FLAG_COHERENT_RR);
		ImGui::CheckboxFlags("Coherent RNG", &mSamplingFlags, BDPT_FLAG_COHERENT_RNG);
		ImGui::CheckboxFlags("BSDF Sampling", &mSamplingFlags, BDPT_FLAG_SAMPLE_BSDFS);
		ImGui::CheckboxFlags("Connect Light Paths to Views", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_VIEWS);
		ImGui::CheckboxFlags("Connect Light Paths to View Paths", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_LIGHT_PATHS);
		if (!(mSamplingFlags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)))
			ImGui::CheckboxFlags("NEE", &mSamplingFlags, BDPT_FLAG_NEE);

		if (mSamplingFlags & (BDPT_FLAG_NEE|BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
			ImGui::Indent();
			ImGui::CheckboxFlags("Sample Environment Map Directly", &mSamplingFlags, BDPT_FLAG_SAMPLE_ENV_TEXTURE);
			ImGui::CheckboxFlags("Multiple Importance", &mSamplingFlags, BDPT_FLAG_MIS);
			if (mPushConstants.gLightCount > 0)
				ImGui::CheckboxFlags("Sample Light Power", &mSamplingFlags, BDPT_FLAG_SAMPLE_LIGHT_POWER);

			if (mSamplingFlags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
				ImGui::CheckboxFlags("Defer Connections", &mSamplingFlags, BDPT_FLAG_DEFER_CONNECTIONS);
				if (mSamplingFlags & BDPT_FLAG_CONNECT_TO_VIEWS) {
					if (mSamplingFlags & BDPT_FLAG_DEFER_CONNECTIONS)
						ImGui::CheckboxFlags("View Connections Use Visibility", &mSamplingFlags, BDPT_FLAG_LIGHT_TRACE_USE_Z);
					ImGui::SetNextItemWidth(40);
					ImGui::InputScalar("Light Trace Quantization", ImGuiDataType_U32, &mLightTraceQuantization);
				}

				ImGui::CheckboxFlags("Light Vertex Cache", &mSamplingFlags, BDPT_FLAG_LIGHT_VERTEX_CACHE);
				if (mSamplingFlags & BDPT_FLAG_LIGHT_VERTEX_CACHE) {
					uint32_t mn = 1;
					ImGui::SetNextItemWidth(40);
					ImGui::DragScalar("Light Path Count", ImGuiDataType_U32, &mPushConstants.gLightPathCount, 1, &mn);
					ImGui::CheckboxFlags("Light Vertex Reservoir Sampling", &mSamplingFlags, BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS);
					if (mSamplingFlags & BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS) {
						ImGui::SetNextItemWidth(40);
						ImGui::DragScalar("Light Vertex Reservoir M", ImGuiDataType_U32, &mPushConstants.gLightVertexReservoirM);
					}
				}

			}

			if (mSamplingFlags & BDPT_FLAG_NEE) {
				if (mPushConstants.gEnvironmentMaterialAddress != -1 && mPushConstants.gLightCount > 0) {
					ImGui::SetNextItemWidth(40);
					ImGui::DragFloat("Environment Sample Probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
				}
				if (mPushConstants.gLightCount > 0)
					ImGui::CheckboxFlags("Uniform Sphere Sampling", &mSamplingFlags, BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);

				ImGui::CheckboxFlags("Defer NEE Rays", &mSamplingFlags, BDPT_FLAG_DEFER_NEE_RAYS);
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

					ImGui::SetNextItemWidth(40);
					ImGui::DragScalar("Candidate Samples", ImGuiDataType_U32, &mPushConstants.gNEEReservoirM);
					if (mPushConstants.gNEEReservoirM == 0) mPushConstants.gNEEReservoirM = 1;

					ImGui::CheckboxFlags("Temporal Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE);
					ImGui::CheckboxFlags("Spatial Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_SPATIAL_REUSE);

					ImGui::Indent();
					if (mSamplingFlags & BDPT_FLAG_RESERVOIR_SPATIAL_REUSE) {
						ImGui::PushItemWidth(40);
						ImGui::DragScalar("Spatial Samples", ImGuiDataType_U32, &mPushConstants.gNEEReservoirSpatialSamples);
						if (mPushConstants.gNEEReservoirSpatialSamples == 0) mPushConstants.gNEEReservoirSpatialSamples = 1;
						ImGui::DragScalar("Spatial Radius", ImGuiDataType_U32, &mPushConstants.gNEEReservoirSpatialRadius);
						ImGui::PopItemWidth();
					}
					if (mSamplingFlags & (BDPT_FLAG_RESERVOIR_SPATIAL_REUSE|BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE)) {
						ImGui::SetNextItemWidth(40);
						ImGui::DragScalar("Max M", ImGuiDataType_U32, &mPushConstants.gReservoirMaxM);
						if (mPushConstants.gReservoirMaxM == 0) mPushConstants.gReservoirMaxM = 1;
						ImGui::CheckboxFlags("Unbiased Reuse", &mSamplingFlags, BDPT_FLAG_RESERVOIR_UNBIASED_REUSE);
					}
					ImGui::Unindent();

					ImGui::PopItemWidth();
					ImGui::Unindent();
				}
			}
			ImGui::Unindent();
		}

		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		ImGui::Indent();
		if (auto denoiser = mNode.find<Denoiser>(); denoiser)
			if (ImGui::Checkbox("Enable Denoiser", &mDenoise))
				denoiser->reset_accumulation();
		Gui::enum_dropdown("Tone Map", mTonemapPipeline->specialization_constant<uint32_t>("gMode"), (uint32_t)TonemapMode::eTonemapModeCount, [](uint32_t i){ return to_string((TonemapMode)i); });
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, -10, 10);
		ImGui::DragFloat("Exposure Alpha", &mTonemapPipeline->push_constant<float>("gExposureAlpha"), .1f, 0, 1);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant<uint32_t>("gGammaCorrection")));
		ImGui::Unindent();
	}

	if (mPrevFrame && ImGui::CollapsingHeader("Export")) {
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

	// reuse old frame resources
	{
		ProfilerRegion ps("Allocate Frame Resources", commandBuffer);
		if (mCurFrame) {
			mFrameResourcePool.push_front(mCurFrame);
			mPrevFrame = mCurFrame;
		}
		mCurFrame.reset();

		for (auto it = mFrameResourcePool.begin(); it != mFrameResourcePool.end(); it++) {
			if (*it != mPrevFrame && (*it)->mFence->status() == vk::Result::eSuccess) {
				mCurFrame = *it;
				mFrameResourcePool.erase(it);
				break;
			}
		}
		if (mCurFrame && mCurFrame->mSelectionData && mCurFrame->mSelectionDataValid && !ImGui::GetIO().WantCaptureMouse) {
			const uint32_t selectedInstance = mCurFrame->mSelectionData.data()->instance_index();
			if (commandBuffer.mDevice.mInstance.window().pressed_redge(KeyCode::eMouse1)) {
				component_ptr<Inspector> inspector = mNode.node_graph().find_components<Inspector>().front();
				if (selectedInstance == INVALID_INSTANCE || selectedInstance >= mCurFrame->mSceneData->mInstanceNodes.size())
					inspector->select(nullptr);
				else {
					Node* selected = mCurFrame->mSceneData->mInstanceNodes[selectedInstance];
					inspector->select(mNode.node_graph().contains(selected) ? selected : nullptr);
				}
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
	mPushConstants.gLightCount = (uint32_t)mCurFrame->mSceneData->mLightInstanceMap.size();
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
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gLightInstances"), mCurFrame->mSceneData->mLightInstanceMap);
	mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gRayCount"), mRayCount);
	for (const auto& [vol, index] : mCurFrame->mSceneData->mResources.volume_data_map)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVolumes"), index, vol);
	for (const auto& [image, index] : mCurFrame->mSceneData->mResources.image4s)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto& [image, index] : mCurFrame->mSceneData->mResources.image1s)
		mCurFrame->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImage1s"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	mCurFrame->mSceneDescriptors->flush_writes();
	mCurFrame->mSceneDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
}

void BDPT::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views) {
	if (!mCurFrame || !mCurFrame->mSceneData) {
		commandBuffer.clear_color_image(renderTarget, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
		return;
	}

	if (mPauseRendering) {
		if (mCurFrame->mTonemapResult.image()->format() == renderTarget.image()->format())
			commandBuffer.copy_image(mCurFrame->mTonemapResult, renderTarget);
		else
			commandBuffer.blit_image(mCurFrame->mTonemapResult, renderTarget);
		return;
	}

	ProfilerRegion ps("BDPT::render", commandBuffer);

	const vk::Extent3D extent = renderTarget.extent();

	bool has_volumes = false;

	// upload views, compute gViewMediumInstances
	{
		// upload viewdatas
		mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size() * sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		mCurFrame->mViewTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		mCurFrame->mViewInverseTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewInverseTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		for (uint32_t i = 0; i < views.size(); i++) {
			mCurFrame->mViews[i] = views[i].first;
			mCurFrame->mViewTransforms[i] = views[i].second;
			mCurFrame->mViewInverseTransforms[i] = views[i].second.inverse();
		}

		// find if views are inside a volume
		mCurFrame->mViewMediumIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewMediumInstances", views.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		ranges::fill(mCurFrame->mViewMediumIndices, INVALID_INSTANCE);
		mNode.for_each_descendant<Medium>([&](const component_ptr<Medium>& vol) {
			has_volumes = true;
			for (uint32_t i = 0; i < views.size(); i++) {
				const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point( views[i].second.transform_point(float3::Zero()) );
				if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
					mCurFrame->mViewMediumIndices[i] = mCurFrame->mSceneData->mInstanceTransformMap.at(vol.get()).second;
			}
		});
	}

	if (!(mSamplingFlags & BDPT_FLAG_LIGHT_VERTEX_CACHE))
		mPushConstants.gLightPathCount = extent.width * extent.height;

	// per-frame push constants
	BDPTPushConstants push_constants = mPushConstants;
	push_constants.gOutputExtent = uint2(extent.width, extent.height);
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = mCurFrame->mFrameNumber;

	// determine sampling flags
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
			sampling_flags &= ~(BDPT_FLAG_NEE|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS|BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_DEFER_CONNECTIONS);
		else if (sampling_flags & (BDPT_FLAG_CONNECT_TO_LIGHT_PATHS|BDPT_FLAG_CONNECT_TO_VIEWS)) {
			sampling_flags |= BDPT_FLAG_UNIFORM_SPHERE_SAMPLING;
			sampling_flags &= ~BDPT_FLAG_NEE; // no NEE when using bdpt
			if (!(sampling_flags & BDPT_FLAG_LIGHT_VERTEX_CACHE))
				sampling_flags &= ~BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS;
		} else
			sampling_flags &= ~(BDPT_FLAG_DEFER_CONNECTIONS|BDPT_FLAG_LIGHT_VERTEX_CACHE|BDPT_FLAG_LIGHT_VERTEX_RESERVOIRS);

		if (!(sampling_flags & BDPT_FLAG_NEE)) {
			sampling_flags &= ~BDPT_FLAG_PRESAMPLE_LIGHTS;
			sampling_flags &= ~BDPT_FLAG_DEFER_NEE_RAYS;
		}

		if (!mPrevFrame || !mPrevFrame->mPathData.contains("gReservoirs") || !mPrevFrame->mPathData.at("gReservoirs"))
			sampling_flags &= ~(BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE);

		for (auto& p : mRenderPipelines)
			p->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		mRenderPipelines[ePresampleLights]->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags | BDPT_FLAG_UNIFORM_SPHERE_SAMPLING;
		mRenderPipelines[eSamplePhotons]->specialization_constant<uint32_t>("gSpecializationFlags") = (sampling_flags | BDPT_FLAG_UNIFORM_SPHERE_SAMPLING | BDPT_FLAG_TRACE_LIGHT) & ~BDPT_FLAG_RAY_CONES;
	}
	for (auto& p : mRenderPipelines) {
		p->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
		p->specialization_constant<uint32_t>("gKernelIterationCount") = mPathTraceKernelIterations;
		p->specialization_constant<uint32_t>("gLightTraceQuantization") = mLightTraceQuantization;
		if (mForceLambertian)
			p->specialization_constant<uint32_t>("FORCE_LAMBERTIAN") = mForceLambertian;
		else
			p->erase_specialization_constant("FORCE_LAMBERTIAN");
	}

	// allocate data
	{
		const uint32_t pixel_count = extent.width * extent.height;
		if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
			ProfilerRegion ps("Allocate data");

			const vk::Format fmt = mHalfColorPrecision ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
			mCurFrame->mRadiance 	  = make_shared<Image>(commandBuffer.mDevice, "gRadiance",   extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
			mCurFrame->mAlbedo 		  = make_shared<Image>(commandBuffer.mDevice, "gAlbedo",     extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
			mCurFrame->mPrevUVs 	  = make_shared<Image>(commandBuffer.mDevice, "gPrevUVs",    extent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
			mCurFrame->mDebugImage 	  = make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
			mCurFrame->mTonemapResult = make_shared<Image>(commandBuffer.mDevice, "gOutput",     extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

			mCurFrame->mPathData["gVisibility"] 		= make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", 		pixel_count * sizeof(VisibilityInfo), 		   vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gPathStates"] 		= make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", 		pixel_count * sizeof(PathState), 			   vk::BufferUsageFlagBits::eStorageBuffer,                                       VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gPathStates1"] 		= make_shared<Buffer>(commandBuffer.mDevice, "gPathStates1",		pixel_count * sizeof(PathState1), 			   vk::BufferUsageFlagBits::eStorageBuffer,                                       VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gRayDifferentials"] 	= make_shared<Buffer>(commandBuffer.mDevice, "gRayDifferentials",   pixel_count * sizeof(RayDifferential), 		   vk::BufferUsageFlagBits::eStorageBuffer,                                       VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gReservoirs"] 		= make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", 		pixel_count * sizeof(Reservoir), 			   vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gReservoirSamples"]	= make_shared<Buffer>(commandBuffer.mDevice, "gReservoirSamples",   pixel_count * sizeof(uint4), 	    		   vk::BufferUsageFlagBits::eStorageBuffer,                                       VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gLightTraceSamples"]  = make_shared<Buffer>(commandBuffer.mDevice, "gLightTraceSamples",  pixel_count * sizeof(float4), 				   vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mFrameNumber = 0;

			mCurFrame->mTonemapMax    = make_shared<Buffer>(commandBuffer.mDevice, "gMax", sizeof(uint4)*3, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mSelectionData = make_shared<Buffer>(commandBuffer.mDevice, "gSelectionData", sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
		}

		const uint32_t light_vertex_count = (sampling_flags & (BDPT_FLAG_CONNECT_TO_LIGHT_PATHS|BDPT_FLAG_DEFER_CONNECTIONS)) ? pixel_count * max(mPushConstants.gMaxPathVertices,1u) : 1;
		if (!mCurFrame->mPathData["gLightPathVertices"] || mCurFrame->mPathData.at("gLightPathVertices").size_bytes() < light_vertex_count * sizeof(PathVertex)) {
			mCurFrame->mPathData["gLightPathVertices"]  = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices",  light_vertex_count * sizeof(PathVertex), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
			mCurFrame->mPathData["gLightPathVertexCount"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertexCount",  sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		}
		const uint32_t view_vertex_count = (sampling_flags & BDPT_FLAG_DEFER_CONNECTIONS) ? pixel_count * max(mPushConstants.gMaxPathVertices,1u) : 1;
		if (!mCurFrame->mPathData["gViewPathVertices"] || mCurFrame->mPathData.at("gViewPathVertices").size_bytes() < view_vertex_count * sizeof(PathVertex))
			mCurFrame->mPathData["gViewPathVertices"]  = make_shared<Buffer>(commandBuffer.mDevice, "gViewPathVertices",  view_vertex_count * sizeof(PathVertex), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);

		const uint32_t nee_ray_count = (sampling_flags & BDPT_FLAG_DEFER_NEE_RAYS) ? pixel_count * (max(mPushConstants.gMaxPathVertices,3u)-2) : 1;
		if (!mCurFrame->mPathData["gNEERays"] || mCurFrame->mPathData.at("gNEERays").size_bytes() < nee_ray_count * sizeof(NEERayData))
			mCurFrame->mPathData["gNEERays"] = make_shared<Buffer>(commandBuffer.mDevice, "gNEERays", nee_ray_count * sizeof(NEERayData), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);

		const uint32_t presampled_light_count = (sampling_flags & BDPT_FLAG_PRESAMPLE_LIGHTS) ? max(1u,mPushConstants.gLightPresampleTileCount*mPushConstants.gLightPresampleTileSize) : 1;
		if (!mCurFrame->mPathData["gPresampledLights"] || mCurFrame->mPathData.at("gPresampledLights").size() < presampled_light_count)
			mCurFrame->mPathData["gPresampledLights"] = make_shared<Buffer>(commandBuffer.mDevice, "gPresampledLights", presampled_light_count * sizeof(PresampledLightPoint), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
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

	// trace light paths
	if (sampling_flags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS) && push_constants.gMaxPathVertices > 2) {
		auto lt = mCurFrame->mPathData.at("gLightTraceSamples");
		auto lv = mCurFrame->mPathData.at("gLightPathVertices");
		auto lc = mCurFrame->mPathData.at("gLightPathVertexCount");
		commandBuffer->fillBuffer(**lt.buffer(), lt.offset(), lt.size_bytes(), 0);
		commandBuffer->fillBuffer(**lv.buffer(), lv.offset(), lv.size_bytes(), 0);
		commandBuffer->fillBuffer(**lc.buffer(), lc.offset(), lc.size_bytes(), 0);
		commandBuffer.barrier({ lv, lt, lc }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		//vk::Extent3D lightExtent;
		//const uint32_t n = (mPushConstants.gLightPathCount + 31) / 32;
		//lightExtent.height = (uint32_t)sqrt((float)n);
		//lightExtent.width = (n + (lightExtent.height-1)) / lightExtent.height;
		//lightExtent.depth = 1;

		{
			ProfilerRegion ps("Sample photons", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[eSamplePhotons]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Sample photons");
			commandBuffer.dispatch_over(extent);
		}

		if (mPathTraceKernelIterations > 0) {
			ProfilerRegion ps("Trace light paths", commandBuffer);
			mRenderPipelines[ePathTraceLoop]->specialization_constant<uint32_t>("gSpecializationFlags") = (sampling_flags | BDPT_FLAG_TRACE_LIGHT) & ~BDPT_FLAG_RAY_CONES;
			commandBuffer.bind_pipeline(mRenderPipelines[ePathTraceLoop]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			for (uint i = 2; i <= push_constants.gMaxPathVertices; i += mPathTraceKernelIterations) {
				commandBuffer.barrier({
					mCurFrame->mPathData.at("gPathStates"),
					mCurFrame->mPathData.at("gPathStates1"),
				}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace light paths");
				commandBuffer.dispatch_over(extent);
			}
		}

		mRenderPipelines[ePathTraceLoop]->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		push_constants.gOutputExtent = uint2(extent.width, extent.height);
	}

	// presample lights
	if (push_constants.gMaxPathVertices > 2 && (sampling_flags & BDPT_FLAG_PRESAMPLE_LIGHTS)) {
		ProfilerRegion ps("Presample lights", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[ePresampleLights]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Presample_lights");
		commandBuffer.dispatch_over(mPushConstants.gLightPresampleTileSize * mPushConstants.gLightPresampleTileCount);
	}

	// barriers + clearing
	{
		if (sampling_flags & BDPT_FLAG_DEFER_CONNECTIONS) {
			auto lv = mCurFrame->mPathData.at("gViewPathVertices");
			commandBuffer->fillBuffer(**lv.buffer(), lv.offset(), lv.size_bytes(), 0);
			commandBuffer.barrier({ lv }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		} else if (sampling_flags & BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)
			commandBuffer.barrier({ mCurFrame->mPathData.at("gLightPathVertices"), mCurFrame->mPathData.at("gLightPathVertexCount") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		if (mPathTraceKernelIterations > 0 && sampling_flags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
			commandBuffer.barrier({
				mCurFrame->mPathData.at("gPathStates"),
				mCurFrame->mPathData.at("gPathStates1")
			}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		}

		if (sampling_flags & BDPT_FLAG_PRESAMPLE_LIGHTS)
			commandBuffer.barrier({ mCurFrame->mPathData.at("gPresampledLights") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		if (sampling_flags & BDPT_FLAG_DEFER_NEE_RAYS) {
			auto v = mCurFrame->mPathData.at("gNEERays");
			commandBuffer->fillBuffer(**v.buffer(), v.offset(), v.size_bytes(), 0);
			commandBuffer.barrier({ mCurFrame->mPathData.at("gNEERays") }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite);
		}

		if (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) {
			commandBuffer.barrier({
				mPrevFrame->mPathData.at("gVisibility"),
				mPrevFrame->mPathData.at("gReservoirs"),
				mPrevFrame->mPathData.at("gReservoirSamples"),
			}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		}
	}

	// trace view paths
	{
		// trace visibility
		{
			ProfilerRegion ps("Sample visibility", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[eSampleVisibility]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Sample visibility");
			commandBuffer.dispatch_over(extent);
		}

		mCurFrame->mPrevUVs.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

		// trace view paths
		if (push_constants.gMaxPathVertices > 2 && mPathTraceKernelIterations > 0) {
			ProfilerRegion ps("Trace view paths", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[ePathTraceLoop]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			for (uint i = 2; i <= push_constants.gMaxPathVertices; i += mPathTraceKernelIterations) {
				mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
				commandBuffer.barrier({
					mCurFrame->mPathData.at("gPathStates"),
					mCurFrame->mPathData.at("gPathStates1"),
					mCurFrame->mPathData.at("gRayDifferentials")
				}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace view paths");
				commandBuffer.dispatch_over(extent);
			}
		}

		// trace nee rays
		if (sampling_flags & BDPT_FLAG_DEFER_NEE_RAYS) {
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mPathData.at("gNEERays") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
			ProfilerRegion ps("Trace NEE rays", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[eTraceNEE]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace NEE rays");
			commandBuffer.dispatch_over(extent);
		}
	}

	// trace bdpt connections
	if (sampling_flags & BDPT_FLAG_DEFER_CONNECTIONS) {
		commandBuffer.barrier({
			mCurFrame->mPathData.at("gVisibility"),
			mCurFrame->mPathData.at("gLightPathVertices"),
			mCurFrame->mPathData.at("gViewPathVertices")
		}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		ProfilerRegion ps("Vertex connections", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[eConnect]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Vertex connections");
		commandBuffer.dispatch_over(extent);
	}

	if (sampling_flags & BDPT_FLAG_CONNECT_TO_VIEWS) {
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		commandBuffer.barrier({ mCurFrame->mPathData.at("gLightTraceSamples") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		ProfilerRegion ps("Add light trace", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[eAddLightTrace]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Add light trace");
		commandBuffer.dispatch_over(extent);
	}

	Image::View result = (mDebugMode == BDPTDebugMode::eNone)  ? mCurFrame->mRadiance : mCurFrame->mDebugImage;

	// accumulate/denoise
	{
		component_ptr<Denoiser> denoiser = mNode.find<Denoiser>();
		const bool reprojection = denoiser ? denoiser->reprojection() : false;
		const bool changed = mPrevFrame && mPrevFrame->mViewTransforms && (mCurFrame->mViewTransforms[0].m != mPrevFrame->mViewTransforms[0].m).any();

		if (changed && (sampling_flags & (BDPT_FLAG_RESERVOIR_TEMPORAL_REUSE|BDPT_FLAG_RESERVOIR_SPATIAL_REUSE)) && !reprojection) {
			// reset reservoir history
			commandBuffer.barrier({mCurFrame->mPathData.at("gReservoirs")}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
			commandBuffer->fillBuffer(**mCurFrame->mPathData.at("gReservoirs").buffer(), mCurFrame->mPathData.at("gReservoirs").offset(), mCurFrame->mPathData.at("gReservoirs").size_bytes(), 0);
		}

		if (mDenoise && denoiser) {
			if (changed && !reprojection) denoiser->reset_accumulation();
			mCurFrame->mDenoiseResult = denoiser->denoise(commandBuffer, result, mCurFrame->mAlbedo, mCurFrame->mViews, mCurFrame->mPathData.at("gVisibility").cast<VisibilityInfo>(), mCurFrame->mPrevUVs);
			result = mCurFrame->mDenoiseResult;

			mTonemapMaxReducePipeline->specialization_constant<uint32_t>("gModulateAlbedo") = denoiser->demodulate_albedo();
			mTonemapPipeline->specialization_constant<uint32_t>("gModulateAlbedo") = denoiser->demodulate_albedo();
		}
	}

	// tone map
	{
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Tonemap");
		ProfilerRegion ps("Tonemap", commandBuffer);

		{
			ProfilerRegion ps("Tonemap Reduce", commandBuffer);
			commandBuffer->fillBuffer(**mCurFrame->mTonemapMax.buffer(), mCurFrame->mTonemapMax.offset(), mCurFrame->mTonemapMax.size_bytes(), 0);
			commandBuffer.barrier({mCurFrame->mTonemapMax}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			mTonemapMaxReducePipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapMaxReducePipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapMaxReducePipeline->descriptor("gMax") = mCurFrame->mTonemapMax;
			commandBuffer.bind_pipeline(mTonemapMaxReducePipeline->get_pipeline());
			mTonemapMaxReducePipeline->bind_descriptor_sets(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mTonemapResult.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.barrier({mCurFrame->mTonemapMax}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite,  vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		mTonemapPipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mTonemapResult, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gMax") = mCurFrame->mTonemapMax;
		mTonemapPipeline->descriptor("gPrevMax") = mPrevFrame && mPrevFrame->mTonemapMax ? mPrevFrame->mTonemapMax : mCurFrame->mTonemapMax;
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
		mCurFrame->mSelectionDataValid = false;
		for (const auto&[view, transform] : views)
			if (view.test_inside(cp)) {
				commandBuffer.copy_buffer(Buffer::View<VisibilityInfo>(v, cp.y() * view.extent().x() + cp.x(), 1), mCurFrame->mSelectionData);
				mCurFrame->mSelectionDataValid = true;
			}
	}

	commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eTopOfPipe, "BDPT::render done");
}

}