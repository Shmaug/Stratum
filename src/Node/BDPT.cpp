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
		mSamplingFlags = 0;
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eRemapThreads);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eRayCones);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eSampleBSDFs);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eCoherentRR);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eNormalMaps);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eNEE);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eMIS);
		BDPT_SET_FLAG(mSamplingFlags, BDPTFlagBits::eDeferShadowRays);
		mPushConstants.gMinPathVertices = 4;
		mPushConstants.gMaxPathVertices = 8;
		mPushConstants.gMaxDiffuseVertices = 2;
		mPushConstants.gMaxNullCollisions = 64;
		mPushConstants.gEnvironmentSampleProbability = 0.5f;
		mPushConstants.gLightPresampleTileSize = 1024;
		mPushConstants.gLightPresampleTileCount = 128;
		mPushConstants.gLightPathCount = 64;
		mPushConstants.gReservoirM = 16;
		mPushConstants.gReservoirMaxM = 64;
		mPushConstants.gReservoirSpatialM = 4;
		mPushConstants.gHashGridBucketCount = 200000;
		mPushConstants.gHashGridMinBucketRadius = 0.1f;
		mPushConstants.gHashGridBucketPixelRadius = 6;

		if (auto arg = instance->find_argument("minPathVertices"); arg)              mPushConstants.gMinPathVertices = atoi(arg->c_str());
		if (auto arg = instance->find_argument("maxPathVertices"); arg)              mPushConstants.gMaxPathVertices = atoi(arg->c_str());
		if (auto arg = instance->find_argument("maxDiffuseVertices"); arg)           mPushConstants.gMaxDiffuseVertices = atoi(arg->c_str());
		if (auto arg = instance->find_argument("maxNullCollisions"); arg)            mPushConstants.gMaxNullCollisions = atoi(arg->c_str());
		if (auto arg = instance->find_argument("environmentSampleProbability"); arg) mPushConstants.gEnvironmentSampleProbability = atof(arg->c_str());
		if (auto arg = instance->find_argument("lightPresampleTileSize"); arg)       mPushConstants.gLightPresampleTileSize = atoi(arg->c_str());
		if (auto arg = instance->find_argument("lightPresampleTileCount"); arg)      mPushConstants.gLightPresampleTileCount = atoi(arg->c_str());
		if (auto arg = instance->find_argument("lightPathCount"); arg)               mPushConstants.gLightPathCount = atoi(arg->c_str());
		if (auto arg = instance->find_argument("reservoirM"); arg)                   mPushConstants.gReservoirM = atoi(arg->c_str());
		if (auto arg = instance->find_argument("reservoirMaxM"); arg)                mPushConstants.gReservoirMaxM = atoi(arg->c_str());
		if (auto arg = instance->find_argument("reservoirSpatialM"); arg)            mPushConstants.gReservoirSpatialM = atoi(arg->c_str());
		if (auto arg = instance->find_argument("hashGridBucketCount"); arg)          mPushConstants.gHashGridBucketCount = atoi(arg->c_str());
		if (auto arg = instance->find_argument("hashGridMinBucketRadius"); arg)      mPushConstants.gHashGridMinBucketRadius = atof(arg->c_str());
		if (auto arg = instance->find_argument("hashGridBucketPixelRadius"); arg)    mPushConstants.gHashGridBucketPixelRadius = atoi(arg->c_str());
		if (auto arg = instance->find_argument("minPathVertices"); arg)              mPushConstants.gMinPathVertices = atoi(arg->c_str());
		if (auto arg = instance->find_argument("maxPathVertices"); arg)              mPushConstants.gMaxPathVertices = atoi(arg->c_str());
		if (auto arg = instance->find_argument("exposure"); arg) exposure = atof(arg->c_str());
		if (auto arg = instance->find_argument("exposureAlpha"); arg) exposure_alpha = atof(arg->c_str());

		for (string arg : instance->find_arguments("bdptFlag")) {
			if (arg.empty()) continue;

			bool set = true;
			if (arg[0] == '~' || arg[0] == '!') {
				set = false;
				arg = arg.substr(1);
			}

			for (char& c : arg) c = tolower(c);

			BDPTFlagBits flag;
			for (uint32_t i = 0; i < BDPTFlagBits::eBDPTFlagCount; i++) {
				const string flag_str = to_string((BDPTFlagBits)i);

				vector<char> str;
				str.reserve(flag_str.size()+1);
				memset(str.data(), 0, flag_str.size()+1);
				for (char c : flag_str)
					if (c != ' ')
						str.emplace_back(tolower(c));

				if (strcmp(arg.data(), str.data()) == 0) {
					if (set)
						BDPT_SET_FLAG(mSamplingFlags, i);
					else
						BDPT_UNSET_FLAG(mSamplingFlags, i);
					break;
				}
			}
		}
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
		const vector<string>& args_rt = { "-matrix-layout-row-major", "-capability", "spirv_1_5" };
		const vector<string>& args = { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" };
		process_shader(mRenderPipelines[eSamplePhotons]         , src_path, "sample_photons"           , args_rt);
		process_shader(mRenderPipelines[eSampleVisibility]      , src_path, "sample_visibility"        , args_rt);
		process_shader(mRenderPipelines[eTraceShadows]          , src_path, "trace_shadows"            , args_rt);
		process_shader(mRenderPipelines[ePresampleLights]       , src_path, "presample_lights"         , args);
		process_shader(mRenderPipelines[eHashGridComputeIndices], src_path, "hashgrid_compute_indices" , args);
		process_shader(mRenderPipelines[eHashGridSwizzle]       , src_path, "hashgrid_swizzle"         , args);
		process_shader(mRenderPipelines[eAddLightTrace]         , src_path, "add_light_trace"          , args);

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

	mRayCount = make_shared<Buffer>(instance->device(), "gCounters", 2*sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	mPrevRayCount.resize(2);
	mRaysPerSecond.resize(2);
	memset(mRayCount.data(), 0, mRayCount.size_bytes());
	memset(mPrevRayCount.data(), 0, mPrevRayCount.size()*sizeof(uint32_t));
	mRayCountTimer = 0;
}

void BDPT::on_inspector_gui() {
	if (mTonemapPipeline && ImGui::Button("Reload BDPT shaders")) {
		mTonemapPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	ImGui::SetNextItemWidth(200);
	Gui::enum_dropdown("BDPT debug mode", mDebugMode, (uint32_t)BDPTDebugMode::eDebugModeCount, [](uint32_t i) { return to_string((BDPTDebugMode)i); });
	if (mDebugMode == BDPTDebugMode::ePathLengthContribution) {
		ImGui::Indent();
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("View path length", ImGuiDataType_U32, &mPushConstants.gDebugViewPathLength, 1);
		ImGui::DragScalar("Light path length", ImGuiDataType_U32, &mPushConstants.gDebugLightPathLength, 1);
		ImGui::PopItemWidth();
		ImGui::Unindent();
	}

	if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::ePerformanceCounters)) {
		const auto [rps, ext] = format_number(mRaysPerSecond[0]);
		ImGui::Text("%.2f%s Rays/second (%u%% shadow rays)", rps, ext, (uint32_t)(100 - (100*(uint64_t)mRaysPerSecond[1]) / mRaysPerSecond[0]));
	}

	ImGui::Checkbox("Pause rendering", &mPauseRendering);

	if (ImGui::CollapsingHeader("Configuration")) {
		ImGui::Checkbox("Random frame seed", &mRandomPerFrame);
		ImGui::Checkbox("Half precision", &mHalfColorPrecision);
		ImGui::Checkbox("Force lambertian", &mForceLambertian);
		for (uint i = 0; i < BDPTFlagBits::eBDPTFlagCount; i++)
			ImGui::CheckboxFlags(to_string((BDPTFlagBits)i).c_str(), &mSamplingFlags, BIT(i));
	}

	if (ImGui::CollapsingHeader("Path tracing")) {
		ImGui::PushItemWidth(60);
		ImGui::DragScalar("Max Path vertices", ImGuiDataType_U32, &mPushConstants.gMaxPathVertices);
		ImGui::DragScalar("Min path vertices", ImGuiDataType_U32, &mPushConstants.gMinPathVertices);
		ImGui::DragScalar("Max diffuse vertices", ImGuiDataType_U32, &mPushConstants.gMaxDiffuseVertices);
		ImGui::DragScalar("Max null collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions);

		if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eConnectToViews))
			ImGui::InputScalar("Light trace quantization", ImGuiDataType_U32, &mLightTraceQuantization);

		if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eLVC)) {
			uint32_t mn = 1;
			ImGui::DragScalar("Light path count", ImGuiDataType_U32, &mPushConstants.gLightPathCount, 1, &mn);
		}

		if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eNEE) && mPushConstants.gEnvironmentMaterialAddress != -1 && mPushConstants.gLightCount > 0)
			ImGui::DragFloat("Environment sample probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);

		if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::ePresampleLights)) {
			ImGui::Indent();
			ImGui::DragScalar("Presample tile size", ImGuiDataType_U32, &mPushConstants.gLightPresampleTileSize);
			ImGui::DragScalar("Presample tile count", ImGuiDataType_U32, &mPushConstants.gLightPresampleTileCount);
			mPushConstants.gLightPresampleTileSize  = max(mPushConstants.gLightPresampleTileSize, 1u);
			mPushConstants.gLightPresampleTileCount = max(mPushConstants.gLightPresampleTileCount, 1u);
			const auto [n, ext] = format_number(mPushConstants.gLightPresampleTileSize*mPushConstants.gLightPresampleTileCount*sizeof(PresampledLightPoint));
			ImGui::Text("Presampled light buffer is %.2f%s bytes", n, ext);
			ImGui::Unindent();
		}

		if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eReservoirs)) {
			ImGui::Indent();
			ImGui::DragScalar("Reservoir candidate samples", ImGuiDataType_U32, &mPushConstants.gReservoirM);
			if (mPushConstants.gReservoirM == 0) mPushConstants.gReservoirM = 1;
			if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eReservoirReuse)) {
				ImGui::DragScalar("Spatial candidates", ImGuiDataType_U32, &mPushConstants.gReservoirSpatialM);
				ImGui::DragScalar("Max M", ImGuiDataType_U32, &mPushConstants.gReservoirMaxM);
				ImGui::DragScalar("Bucket count", ImGuiDataType_U32, &mPushConstants.gHashGridBucketCount);
				ImGui::DragFloat("Bucket pixel radius", &mPushConstants.gHashGridBucketPixelRadius);
				ImGui::DragFloat("Min bucket radius", &mPushConstants.gHashGridMinBucketRadius);
				mPushConstants.gHashGridBucketCount = max(mPushConstants.gHashGridBucketCount, 1u);
				if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::ePerformanceCounters)) {
					for (auto it = mFrameResourcePool.begin(); it != mFrameResourcePool.end(); it++) {
						if (*it && (*it)->mFence->status() == vk::Result::eSuccess && (*it)->mPathData["gHashGridStats"]) {
							Buffer::View<uint32_t> data = (*it)->mPathData["gHashGridStats"].cast<uint32_t>();
							ImGui::Text("%u failed inserts", data[0]);
							ImGui::Text("%u%% buckets used", (100*data[1])/mPushConstants.gHashGridBucketCount);
							break;
						}
					}
				}
			}
			ImGui::Unindent();
		}

		ImGui::PopItemWidth();
	}

	if (ImGui::CollapsingHeader("Post processing")) {
		ImGui::Indent();
		if (auto denoiser = mNode.find<Denoiser>(); denoiser)
			if (ImGui::Checkbox("Enable denoiser", &mDenoise))
				denoiser->reset_accumulation();
		Gui::enum_dropdown("Tone map", mTonemapPipeline->specialization_constant<uint32_t>("gMode"), (uint32_t)TonemapMode::eTonemapModeCount, [](uint32_t i){ return to_string((TonemapMode)i); });
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, -10, 10);
		ImGui::DragFloat("Exposure alpha", &mTonemapPipeline->push_constant<float>("gExposureAlpha"), .1f, 0, 1);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant<uint32_t>("gGammaCorrection")));
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

	mRayCountTimer += deltaTime;
	if (mRayCountTimer > 1) {
		for (uint32_t i = 0; i < mRaysPerSecond.size(); i++)
			mRaysPerSecond[i] = (mRayCount[i] - mPrevRayCount[i]) / mRayCountTimer;
		ranges::copy(mRayCount, mPrevRayCount.begin());
		mRayCountTimer = 0;
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
		// upload viewdata
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

	if (BDPT_CHECK_FLAG(mSamplingFlags, BDPTFlagBits::eLVC))
		mRenderPipelines[eSampleVisibility]->specialization_constant<uint32_t>("HASHGRID_RESERVOIR_VERTEX") = 1;
	else {
		mRenderPipelines[eSampleVisibility]->erase_specialization_constant("HASHGRID_RESERVOIR_VERTEX");
		mPushConstants.gLightPathCount = extent.width * extent.height;
	}

	component_ptr<Denoiser> denoiser = mNode.find<Denoiser>();
	const bool reprojection = denoiser ? denoiser->reprojection() : false;
	const bool changed = mPrevFrame && mPrevFrame->mViewTransforms && (mCurFrame->mViewTransforms[0].m != mPrevFrame->mViewTransforms[0].m).any();

	// per-frame push constants
	BDPTPushConstants push_constants = mPushConstants;
	push_constants.gOutputExtent = uint2(extent.width, extent.height);
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = mCurFrame->mFrameNumber;

	if ((changed && !reprojection) || !mPrevFrame || !mPrevFrame->mPathData["gHashGridReservoirs"])
		push_constants.gReservoirSpatialM = 0;

	// determine sampling flags
	uint32_t scene_flags = 0;
	uint32_t sampling_flags = mSamplingFlags;
	{
		if (push_constants.gEnvironmentMaterialAddress != -1)
			scene_flags |= BDPT_FLAG_HAS_ENVIRONMENT;
		else
			push_constants.gEnvironmentSampleProbability = 0;

		if (push_constants.gLightCount)
			scene_flags |= BDPT_FLAG_HAS_EMISSIVES;
		else
			push_constants.gEnvironmentSampleProbability = 1;

		if (has_volumes)
			scene_flags |= BDPT_FLAG_HAS_MEDIA;
		else
			push_constants.gMaxNullCollisions = 0;

		if (push_constants.gLightCount == 0 && push_constants.gEnvironmentMaterialAddress == -1) {
			// no lights...
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eNEE);
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eConnectToViews);
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eConnectToLightPaths);
		} else if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToViews) || BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToLightPaths)) {
			// using bidirectional methods
			BDPT_SET_FLAG(sampling_flags, BDPTFlagBits::eUniformSphereSampling);
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eNEE);
			if (!BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eLVC)) {
				BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eReservoirs);
				BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse);
			}
		} else {
			// regular path tracing
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eLVC);
			if (!BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eNEE)) {
				BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eReservoirs);
				BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse);
			}
		}

		if (!BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eNEE)) {
			BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::ePresampleLights);
			if (!BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eLVC))
				BDPT_UNSET_FLAG(sampling_flags, BDPTFlagBits::eDeferShadowRays);
		}
	}
	for (auto& p : mRenderPipelines) {
		p->specialization_constant<uint32_t>("gSceneFlags") = scene_flags;
		p->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		p->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
		p->specialization_constant<uint32_t>("gLightTraceQuantization") = mLightTraceQuantization;
		if (mForceLambertian)
			p->specialization_constant<uint32_t>("FORCE_LAMBERTIAN") = 1;
		else
			p->erase_specialization_constant("FORCE_LAMBERTIAN");
	}
	uint32_t tmp = sampling_flags;
	BDPT_SET_FLAG(tmp, BDPTFlagBits::eUniformSphereSampling);
	BDPT_UNSET_FLAG(tmp, BDPTFlagBits::eRayCones);
	mRenderPipelines[ePresampleLights]->specialization_constant<uint32_t>("gSpecializationFlags") = tmp;
	mRenderPipelines[eSamplePhotons]->specialization_constant<uint32_t>("gSpecializationFlags") = tmp;
	mRenderPipelines[eSamplePhotons]->specialization_constant<uint32_t>("gSceneFlags") = scene_flags | BDPT_FLAG_TRACE_LIGHT;


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
			mCurFrame->mPathData["gRayDifferentials"] 	= make_shared<Buffer>(commandBuffer.mDevice, "gRayDifferentials",   pixel_count * sizeof(RayDifferential), 		   vk::BufferUsageFlagBits::eStorageBuffer,                                       VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gLightTraceSamples"]  = make_shared<Buffer>(commandBuffer.mDevice, "gLightTraceSamples",  pixel_count * sizeof(float4), 				   vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mFrameNumber = 0;

			mCurFrame->mTonemapMax    = make_shared<Buffer>(commandBuffer.mDevice, "gMax", sizeof(uint4)*3, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mSelectionData = make_shared<Buffer>(commandBuffer.mDevice, "gSelectionData", sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
		}

		const uint32_t light_vertex_count = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToLightPaths) ? push_constants.gLightPathCount * (push_constants.gMaxDiffuseVertices+1) : 1;
		if (!mCurFrame->mPathData["gLightPathVertices"] || mCurFrame->mPathData.at("gLightPathVertices").size_bytes() < light_vertex_count * sizeof(PathVertex)) {
			mCurFrame->mPathData["gLightPathVertices"]    = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices",  light_vertex_count * sizeof(PathVertex), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
			mCurFrame->mPathData["gLightPathVertexCount"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertexCount",  sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		}

		const uint32_t shadow_ray_count = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eDeferShadowRays) ? pixel_count * max(push_constants.gMaxDiffuseVertices,1u) : 1;
		if (!mCurFrame->mPathData["gShadowRays"] || mCurFrame->mPathData.at("gShadowRays").size_bytes() < shadow_ray_count * sizeof(ShadowRayData))
			mCurFrame->mPathData["gShadowRays"] = make_shared<Buffer>(commandBuffer.mDevice, "gShadowRays", shadow_ray_count * sizeof(ShadowRayData), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);

		const uint32_t presampled_light_count = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::ePresampleLights) ? max(1u,push_constants.gLightPresampleTileCount*push_constants.gLightPresampleTileSize) : 1;
		if (!mCurFrame->mPathData["gPresampledLights"] || mCurFrame->mPathData.at("gPresampledLights").size_bytes() < presampled_light_count * sizeof(PresampledLightPoint))
			mCurFrame->mPathData["gPresampledLights"] = make_shared<Buffer>(commandBuffer.mDevice, "gPresampledLights", presampled_light_count * sizeof(PresampledLightPoint), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);

		const uint32_t hashgrid_bucket_count = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse) ? max(1u,push_constants.gHashGridBucketCount) : 1;
		if (!mCurFrame->mPathData["gHashGridChecksums"] || mCurFrame->mPathData.at("gHashGridChecksums").size_bytes() < hashgrid_bucket_count * sizeof(uint32_t)) {
			mCurFrame->mPathData["gHashGridChecksums"]      = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridChecksums", hashgrid_bucket_count * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridCounters"]       = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridCounters" , hashgrid_bucket_count * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridIndices"]        = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridIndices" , hashgrid_bucket_count * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridStats"]          = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridStats", 4 * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
		}

		const uint32_t hashgrid_reservoir_count  = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse) ? pixel_count * max(1u,push_constants.gMaxDiffuseVertices) : 1;
		if (!mCurFrame->mPathData["gHashGridReservoirs"] || mCurFrame->mPathData.at("gHashGridReservoirs").size_bytes() < hashgrid_reservoir_count * sizeof(ReservoirData)) {
			mCurFrame->mPathData["gHashGridReservoirs"]             = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridReservoirs"            , hashgrid_reservoir_count * sizeof(ReservoirData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridReservoirSamples"]       = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridReservoirSamples"      , hashgrid_reservoir_count * sizeof(PathVertex), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridAppendIndices"]          = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridAppendIndices"         , hashgrid_reservoir_count * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridAppendReservoirs"]       = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridAppendReservoirs"      , hashgrid_reservoir_count * sizeof(ReservoirData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			mCurFrame->mPathData["gHashGridAppendReservoirSamples"] = make_shared<Buffer>(commandBuffer.mDevice, "gHashGridAppendReservoirSamples", hashgrid_reservoir_count * sizeof(PathVertex), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
		}
	}

	// set descriptors
	{
		mCurFrame->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "bdpt_view_descriptors");
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mCurFrame->mViews);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewTransforms"), mCurFrame->mViewTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gInverseViewTransforms"), mCurFrame->mViewInverseTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), (mPrevFrame && mPrevFrame->mViews ? mPrevFrame : mCurFrame)->mViews);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevInverseViewTransforms"), (mPrevFrame && mPrevFrame->mViews ? mPrevFrame : mCurFrame)->mViewInverseTransforms);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewMediumInstances"), mCurFrame->mViewMediumIndices);
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevUVs"), image_descriptor(mCurFrame->mPrevUVs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gDebugImage"), image_descriptor(mCurFrame->mDebugImage, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		const bool use_prev_reservoirs = BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse) && push_constants.gReservoirSpatialM > 0;
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevHashGridChecksums")       , (use_prev_reservoirs ? mPrevFrame : mCurFrame)->mPathData.at("gHashGridChecksums"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevHashGridCounters")        , (use_prev_reservoirs ? mPrevFrame : mCurFrame)->mPathData.at("gHashGridCounters"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevHashGridIndices")         , (use_prev_reservoirs ? mPrevFrame : mCurFrame)->mPathData.at("gHashGridIndices"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevHashGridReservoirs")      , (use_prev_reservoirs ? mPrevFrame : mCurFrame)->mPathData.at("gHashGridReservoirs"));
		mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevHashGridReservoirSamples"), (use_prev_reservoirs ? mPrevFrame : mCurFrame)->mPathData.at("gHashGridReservoirSamples"));
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
	if ((BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToViews) || BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToLightPaths)) && push_constants.gMaxPathVertices > 2) {
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
	}

	// presample lights
	if (push_constants.gMaxPathVertices > 2 && BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::ePresampleLights)) {
		ProfilerRegion ps("Presample lights", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[ePresampleLights]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Presample_lights");
		commandBuffer.dispatch_over(mPushConstants.gLightPresampleTileSize * mPushConstants.gLightPresampleTileCount);
	}

	// barriers + clearing
	{
		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToLightPaths))
			commandBuffer.barrier({ mCurFrame->mPathData.at("gLightPathVertices"), mCurFrame->mPathData.at("gLightPathVertexCount") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::ePresampleLights))
			commandBuffer.barrier({ mCurFrame->mPathData.at("gPresampledLights") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eDeferShadowRays)) {
			auto v = mCurFrame->mPathData.at("gShadowRays");
			commandBuffer->fillBuffer(**v.buffer(), v.offset(), v.size_bytes(), 0);
			commandBuffer.barrier({ mCurFrame->mPathData.at("gShadowRays") }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite);
		}

		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse)) {
			auto c  = mCurFrame->mPathData.at("gHashGridCounters");
			auto cs = mCurFrame->mPathData.at("gHashGridChecksums");
			auto a  = mCurFrame->mPathData.at("gHashGridAppendIndices");
			commandBuffer->fillBuffer(**c.buffer() , c.offset() , c.size_bytes() , 0);
			commandBuffer->fillBuffer(**cs.buffer(), cs.offset(), cs.size_bytes(), 0);
			commandBuffer->fillBuffer(**a.buffer() , a.offset() , sizeof(uint2)  , 0);
			commandBuffer.barrier({ c, cs, a }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite);
			memset(mCurFrame->mPathData["gHashGridStats"].data(), 0, mCurFrame->mPathData["gHashGridStats"].size_bytes());
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

		// trace shadow rays
		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eDeferShadowRays)) {
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mPathData.at("gShadowRays") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
			ProfilerRegion ps("Trace shadow rays", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[eTraceShadows]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace shadow rays");
			commandBuffer.dispatch_over(extent);
		}

		// trace shadow rays
		if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eReservoirReuse)) {
			{
				commandBuffer.barrier({
					mCurFrame->mPathData.at("gHashGridCounters"),
				}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
				ProfilerRegion ps("Compute hash grid indices", commandBuffer);
				commandBuffer.bind_pipeline(mRenderPipelines[eHashGridComputeIndices]->get_pipeline(mDescriptorSetLayouts));
				bind_descriptors_and_push_constants();
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Compute hash grid indices");
				commandBuffer.dispatch_over(extent.width, (push_constants.gHashGridBucketCount + extent.width-1) / extent.width);
			}
			{
				commandBuffer.barrier({
					mCurFrame->mPathData.at("gHashGridIndices"),
					mCurFrame->mPathData.at("gHashGridAppendReservoirs"),
					mCurFrame->mPathData.at("gHashGridAppendReservoirSamples"),
					mCurFrame->mPathData.at("gHashGridAppendIndices")
				}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
				ProfilerRegion ps("Swizzle hash grid", commandBuffer);
				commandBuffer.bind_pipeline(mRenderPipelines[eHashGridSwizzle]->get_pipeline(mDescriptorSetLayouts));
				bind_descriptors_and_push_constants();
				commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Swizzle hash grid");
				commandBuffer.dispatch_over(extent.width, extent.height * push_constants.gMaxDiffuseVertices);
			}
		}
	}

	// add light trace
	if (BDPT_CHECK_FLAG(sampling_flags, BDPTFlagBits::eConnectToViews)) {
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
	if (mDenoise && denoiser) {
		if (changed && !reprojection) denoiser->reset_accumulation();
		mCurFrame->mDenoiseResult = denoiser->denoise(commandBuffer, result, mCurFrame->mAlbedo, mCurFrame->mViews, mCurFrame->mPathData.at("gVisibility").cast<VisibilityInfo>(), mCurFrame->mPrevUVs);
		result = mCurFrame->mDenoiseResult;

		mTonemapMaxReducePipeline->specialization_constant<uint32_t>("gModulateAlbedo") = denoiser->demodulate_albedo();
		mTonemapPipeline->specialization_constant<uint32_t>("gModulateAlbedo") = denoiser->demodulate_albedo();
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