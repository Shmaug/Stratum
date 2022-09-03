#include "VCM.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

#include <Shaders/tonemap.h>

namespace stm {

void inspector_gui_fn(Inspector& inspector, VCM* v) { v->on_inspector_gui(); }

VCM::VCM(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);
	app->OnUpdate.add_listener(mNode, bind(&VCM::update, this, std::placeholders::_1, std::placeholders::_2), Node::EventPriority::eAlmostLast);

	create_pipelines();
}

void VCM::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	float exposure = 1;
	float exposure_alpha = 0.1f;
	uint32_t tonemapper = 0;
	if (mTonemapPipeline) {
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		exposure_alpha = mTonemapPipeline->push_constant<float>("gExposureAlpha");
		tonemapper = mTonemapPipeline->specialization_constant<uint32_t>("gMode");
	} else {
		mPushConstants.gMinPathLength = 1;
		mPushConstants.gMaxPathLength = 8;
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

		const fs::path& src_path = "../../src/Shaders/kernels/renderers/vcm.hlsl";
		const vector<string>& args = { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" };
		process_shader(mRenderPipelines[eLightTrace]   , src_path, "light_trace"    , args);
		process_shader(mRenderPipelines[eCameraTrace]  , src_path, "camera_trace"   , args);
		process_shader(mRenderPipelines[eAddLightTrace], src_path, "add_light_trace", args);

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
				if (name == "gVolumes" || name == "gImages") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
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

	mRayCount = make_shared<Buffer>(instance->device(), "gRayCount", sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_TO_CPU);
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
	mPrevCounterValue = 0;
}

void VCM::on_inspector_gui() {
	if (mTonemapPipeline && ImGui::Button("Reload VCM Shaders")) {
		mTonemapPipeline->stage(vk::ShaderStageFlagBits::eCompute)->mDevice->waitIdle();
		create_pipelines();
	}

	ImGui::Checkbox("Pause Rendering", &mPauseRendering);
	ImGui::Checkbox("Half Precision", &mHalfColorPrecision);

	ImGui::SetNextItemWidth(200);
	Gui::enum_dropdown("VCM Debug Mode", mDebugMode, (uint32_t)VCMDebugMode::eDebugModeCount, [](uint32_t i) { return to_string((VCMDebugMode)i); });
	if (mDebugMode == VCMDebugMode::ePathLengthContribution) {
		ImGui::Indent();
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("View Path Length", ImGuiDataType_U32, &mPushConstants.gDebugViewPathLength, 1);
		ImGui::DragScalar("Light Path Length", ImGuiDataType_U32, &mPushConstants.gDebugLightPathLength, 1);
		ImGui::PopItemWidth();
		ImGui::Unindent();
	}

	ImGui::CheckboxFlags("Count Rays/second", &mSamplingFlags, VCM_FLAG_COUNT_RAYS);
	if (mSamplingFlags & VCM_FLAG_COUNT_RAYS) {
		const auto [rps, ext] = format_number(mRaysPerSecond);
		ImGui::Text("%.2f%s Rays/second", rps, ext);
	}

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::Indent();

		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Path Length", ImGuiDataType_U32, &mPushConstants.gMaxPathLength);
		ImGui::DragScalar("Min Path Length", ImGuiDataType_U32, &mPushConstants.gMinPathLength);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Remap Threads", &mSamplingFlags, VCM_FLAG_REMAP_THREADS);
		ImGui::CheckboxFlags("Use VC", &mSamplingFlags, VCM_FLAG_USE_VC);
		ImGui::CheckboxFlags("Use MIS", &mSamplingFlags, VCM_FLAG_USE_MIS);
		ImGui::CheckboxFlags("Light Trace Only", &mSamplingFlags, VCM_FLAG_LIGHT_TRACE_ONLY);
		if (!(mSamplingFlags & (VCM_FLAG_USE_VC|VCM_FLAG_LIGHT_TRACE_ONLY|VCM_FLAG_USE_VM|VCM_FLAG_USE_PPM)))
			ImGui::CheckboxFlags("NEE", &mSamplingFlags, VCM_FLAG_USE_NEE);

		ImGui::InputScalar("Light Trace Quantization", ImGuiDataType_U32, &mLightTraceQuantization);

		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		ImGui::Indent();
		if (auto denoiser = mNode.find<Denoiser>(); denoiser)
			if (ImGui::Checkbox("Enable Denoiser", &mDenoise))
				denoiser->reset_accumulation();
		Gui::enum_dropdown("Tone Map", mTonemapPipeline->specialization_constant<uint32_t>("gMode"), (uint32_t)TonemapMode::eTonemapModeCount, [](uint32_t i){ return to_string((TonemapMode)i); });
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::DragFloat("Exposure Alpha", &mTonemapPipeline->push_constant<float>("gExposureAlpha"), .1f, 0, 1);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant<uint32_t>("gGammaCorrection")));
		ImGui::Unindent();
	}

	if (mResources.mPrev && ImGui::CollapsingHeader("Export")) {
		ImGui::Indent();
		static char path[256]{ 'i', 'm', 'a', 'g', 'e', '.', 'h', 'd', 'r', '\0' };
		ImGui::InputText("", path, sizeof(path));
		if (ImGui::Button("Save")) {
			Device& d = mResources.mPrev->mRadiance.image()->mDevice;
			auto cb = d.get_command_buffer("image copy");

			Image::View src = mDenoise ? mResources.mPrev->mDenoiseResult : mResources.mPrev->mRadiance;
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

void VCM::update(CommandBuffer& commandBuffer, const float deltaTime) {
	ProfilerRegion ps("VCM::update", commandBuffer);

	// reuse old frame resources
	mResources.advance(commandBuffer.fence());

	if (mResources.mCur && mResources.mCur->mSelectionData && mResources.mCur->mSelectionDataValid && !ImGui::GetIO().WantCaptureMouse) {
		const uint32_t selectedInstance = mResources.mCur->mSelectionData.data()->instance_index();
		if (commandBuffer.mDevice.mInstance.window().pressed_redge(KeyCode::eMouse1)) {
			component_ptr<Inspector> inspector = mNode.node_graph().find_components<Inspector>().front();
			if (selectedInstance == INVALID_INSTANCE || selectedInstance >= mResources.mCur->mSceneData->mInstanceNodes.size())
				inspector->select(nullptr);
			else {
				Node* selected = mResources.mCur->mSceneData->mInstanceNodes[selectedInstance];
				inspector->select(mNode.node_graph().contains(selected) ? selected : nullptr);
			}
		}
	}

	mResources.mCur->mSceneData = mNode.find_in_ancestor<Scene>()->data();
	if (!mResources.mCur->mSceneData) return;

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mRayCount[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mRayCount[0];
		mRaysPerSecondTimer = 0;
	}

	mPushConstants.gEnvironmentMaterialAddress = mResources.mCur->mSceneData->mEnvironmentMaterialAddress;
	mPushConstants.gLightCount = (uint32_t)mResources.mCur->mSceneData->mLightInstanceMap.size();

	if (!mResources.mCur->mSceneDescriptors) mResources.mCur->mSceneDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[0], "path_tracer_scene_descriptors");
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gScene"), **mResources.mCur->mSceneData->mScene);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVertices"), mResources.mCur->mSceneData->mVertices);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gIndices"), mResources.mCur->mSceneData->mIndices);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstances"), mResources.mCur->mSceneData->mInstances);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceTransforms"), mResources.mCur->mSceneData->mInstanceTransforms);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceInverseTransforms"), mResources.mCur->mSceneData->mInstanceInverseTransforms);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gInstanceMotionTransforms"), mResources.mCur->mSceneData->mInstanceMotionTransforms);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gMaterialData"), mResources.mCur->mSceneData->mMaterialData);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gDistributions"), mResources.mCur->mSceneData->mDistributionData);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gLightInstanceMap"), mResources.mCur->mSceneData->mLightInstanceMap);
	mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gRayCount"), mRayCount);
	for (const auto& [image, index] : mResources.mCur->mSceneData->mResources.images)
		mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto& [vol, index] : mResources.mCur->mSceneData->mResources.volume_data_map)
		mResources.mCur->mSceneDescriptors->insert_or_assign(mDescriptorMap[0].at("gVolumes"), index, vol);
	mResources.mCur->mSceneDescriptors->flush_writes();
	mResources.mCur->mSceneDescriptors->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
}

void VCM::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<pair<ViewData,TransformData>>& views) {
	if (!mResources.mCur || !mResources.mCur->mSceneData) {
		commandBuffer.clear_color_image(renderTarget, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
		return;
	}

	if (mPauseRendering) {
		if (mResources.mCur->mTonemapResult.image()->format() == renderTarget.image()->format())
			commandBuffer.copy_image(mResources.mCur->mTonemapResult, renderTarget);
		else
			commandBuffer.blit_image(mResources.mCur->mTonemapResult, renderTarget);
		return;
	}

	ProfilerRegion ps("VCM::render", commandBuffer);

	const vk::Extent3D extent = renderTarget.extent();

	bool has_volumes = false;

	// upload views, compute gViewMediumInstances
	{
		// upload viewdatas
		mResources.mCur->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size() * sizeof(ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		mResources.mCur->mViewTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		mResources.mCur->mViewInverseTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gViewInverseTransforms", views.size() * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		for (uint32_t i = 0; i < views.size(); i++) {
			mResources.mCur->mViews[i] = views[i].first;
			mResources.mCur->mViewTransforms[i] = views[i].second;
			mResources.mCur->mViewInverseTransforms[i] = views[i].second.inverse();
		}

		// find if views are inside a volume
		mResources.mCur->mViewMediumIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewMediumInstances", views.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		ranges::fill(mResources.mCur->mViewMediumIndices, INVALID_INSTANCE);
		mNode.for_each_descendant<Medium>([&](const component_ptr<Medium>& vol) {
			has_volumes = true;
			for (uint32_t i = 0; i < views.size(); i++) {
				const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point( views[i].second.transform_point(float3::Zero()) );
				if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
					mResources.mCur->mViewMediumIndices[i] = mResources.mCur->mSceneData->mInstanceTransformMap.at(vol.get()).second;
			}
		});
	}

	// per-frame push constants
	VCMPushConstants push_constants = mPushConstants;
	push_constants.gOutputExtent = uint2(extent.width, extent.height);
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = mResources.mCur->mFrameNumber;

	// determine sampling flags
	uint32_t sampling_flags = mSamplingFlags;
	{
		if (push_constants.gEnvironmentMaterialAddress == -1)
			sampling_flags &= ~VCM_FLAG_HAS_ENVIRONMENT;
		else
			sampling_flags |= VCM_FLAG_HAS_ENVIRONMENT;

		if (push_constants.gLightCount)
			sampling_flags |= VCM_FLAG_HAS_EMISSIVES;
		else
			sampling_flags &= ~VCM_FLAG_HAS_EMISSIVES;

		if (has_volumes)
			sampling_flags |= VCM_FLAG_HAS_MEDIA;
		else {
			sampling_flags &= ~VCM_FLAG_HAS_MEDIA;
			push_constants.gMaxNullCollisions = 0;
		}
		if (mSamplingFlags & (VCM_FLAG_USE_VC|VCM_FLAG_LIGHT_TRACE_ONLY|VCM_FLAG_USE_VM|VCM_FLAG_USE_PPM))
			sampling_flags &= ~VCM_FLAG_USE_NEE;
	}
	for (auto& p : mRenderPipelines) {
		p->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		p->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
		p->specialization_constant<uint32_t>("gLightTraceQuantization") = mLightTraceQuantization;
	}

	// allocate data
	{
		const uint32_t pixel_count = extent.width * extent.height;
		if (!mResources.mCur->mRadiance || mResources.mCur->mRadiance.extent() != extent) {
			ProfilerRegion ps("Allocate data");

			const vk::Format fmt = mHalfColorPrecision ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
			mResources.mCur->mRadiance 	    = make_shared<Image>(commandBuffer.mDevice, "gRadiance",   extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
			mResources.mCur->mAlbedo 		= make_shared<Image>(commandBuffer.mDevice, "gAlbedo",     extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
			mResources.mCur->mPrevUVs 	    = make_shared<Image>(commandBuffer.mDevice, "gPrevUVs",    extent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
			mResources.mCur->mDebugImage 	= make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
			mResources.mCur->mTonemapResult = make_shared<Image>(commandBuffer.mDevice, "gOutput",     extent, fmt,                       1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);

			mResources.mCur->mTonemapMax    = make_shared<Buffer>(commandBuffer.mDevice, "gMax", sizeof(uint4)*3, vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY),
			mResources.mCur->mSelectionData = make_shared<Buffer>(commandBuffer.mDevice, "gSelectionData", sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU),
			mResources.mCur->mPathData["gVisibility"] 		  = make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", 	      pixel_count * sizeof(VisibilityInfo), 		 vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_GPU_ONLY);
			mResources.mCur->mPathData["gLightTraceSamples"]  = make_shared<Buffer>(commandBuffer.mDevice, "gLightTraceSamples",  pixel_count * sizeof(float4), 				 vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY);
			mResources.mCur->mPathData["gPathLengths"]        = make_shared<Buffer>(commandBuffer.mDevice, "gPathLengths",        pixel_count * sizeof(uint32_t),                vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
			mResources.mCur->mFrameNumber = 0;
		}
		const uint32_t light_vertex_count = pixel_count * max(mPushConstants.gMaxPathLength,1u);
		if (!mResources.mCur->mPathData["gLightVertices"] || mResources.mCur->mPathData.at("gLightVertices").size_bytes() < light_vertex_count * sizeof(VcmVertex))
			mResources.mCur->mPathData["gLightVertices"]  = make_shared<Buffer>(commandBuffer.mDevice, "gLightVertices",  light_vertex_count * sizeof(VcmVertex),  vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32);
	}

	// set descriptors
	{
		mResources.mCur->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "vcm_view_descriptors");
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mResources.mCur->mViews);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewTransforms"), mResources.mCur->mViewTransforms);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gInverseViewTransforms"), mResources.mCur->mViewInverseTransforms);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), (mResources.mPrev && mResources.mPrev->mViews) ? mResources.mPrev->mViews : mResources.mCur->mViews);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevInverseViewTransforms"), (mResources.mPrev && mResources.mPrev->mViews) ? mResources.mPrev->mViewInverseTransforms : mResources.mCur->mViewInverseTransforms);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevVisibility"), (mResources.mPrev && mResources.mPrev->mViews) ? mResources.mPrev->mPathData.at("gVisibility") : mResources.mCur->mPathData.at("gVisibility"));
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewMediumInstances"), mResources.mCur->mViewMediumIndices);
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mResources.mCur->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mResources.mCur->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevUVs"), image_descriptor(mResources.mCur->mPrevUVs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gDebugImage"), image_descriptor(mResources.mCur->mDebugImage, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		for (const auto&[name, buf] : mResources.mCur->mPathData)
			mResources.mCur->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at(name), buf);
	}

	auto bind_descriptors_and_push_constants = [&]() {
		commandBuffer.bind_descriptor_set(0, mResources.mCur->mSceneDescriptors);
		commandBuffer.bind_descriptor_set(1, mResources.mCur->mViewDescriptors);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(VCMPushConstants), &push_constants);
	};

	if (mDebugMode != VCMDebugMode::eNone)
		commandBuffer.clear_color_image(mResources.mCur->mDebugImage, vk::ClearColorValue(array<uint32_t, 4>{ 0, 0, 0, 0 }));
	mResources.mCur->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mResources.mCur->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mResources.mCur->mPrevUVs.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mResources.mCur->mDebugImage.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

	// trace light paths
	if (sampling_flags & (VCM_FLAG_USE_VC|VCM_FLAG_USE_VM|VCM_FLAG_LIGHT_TRACE_ONLY)) {
		auto lt = mResources.mCur->mPathData.at("gLightTraceSamples");
		commandBuffer->fillBuffer(**lt.buffer(), lt.offset(), lt.size_bytes(), 0);
		commandBuffer.barrier({ lt }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		{
			ProfilerRegion ps("Trace light paths", commandBuffer);
			commandBuffer.bind_pipeline(mRenderPipelines[eLightTrace]->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Trace light paths");
			commandBuffer.dispatch_over(extent);
		}

		commandBuffer.barrier({
			mResources.mCur->mPathData.at("gLightVertices"),
			mResources.mCur->mPathData.at("gPathLengths")
		}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}

	// trace camera paths
	if (!(sampling_flags & VCM_FLAG_LIGHT_TRACE_ONLY)){
		ProfilerRegion ps("Trace camera paths", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[eCameraTrace]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Sample visibility");
		commandBuffer.dispatch_over(extent);
	}

	// add light and camera trace
	if (sampling_flags & (VCM_FLAG_USE_VC|VCM_FLAG_USE_VM|VCM_FLAG_LIGHT_TRACE_ONLY)) {
		mResources.mCur->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
		commandBuffer.barrier({ mResources.mCur->mPathData.at("gLightTraceSamples") }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		ProfilerRegion ps("Add light trace", commandBuffer);
		commandBuffer.bind_pipeline(mRenderPipelines[eAddLightTrace]->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eComputeShader, "Add light trace");
		commandBuffer.dispatch_over(extent);
	}

	Image::View result = (mDebugMode == VCMDebugMode::eNone)  ? mResources.mCur->mRadiance : mResources.mCur->mDebugImage;

	// accumulate/denoise
	{
		component_ptr<Denoiser> denoiser = mNode.find<Denoiser>();
		const bool reprojection = denoiser ? denoiser->reprojection() : false;
		const bool changed = mResources.mPrev && mResources.mPrev->mViewTransforms && (mResources.mCur->mViewTransforms[0].m != mResources.mPrev->mViewTransforms[0].m).any();

		if (mDenoise && denoiser) {
			if (changed && !reprojection) denoiser->reset_accumulation();
			mResources.mCur->mDenoiseResult = denoiser->denoise(commandBuffer, result, mResources.mCur->mAlbedo, mResources.mCur->mViews, mResources.mCur->mPathData.at("gVisibility").cast<VisibilityInfo>(), mResources.mCur->mPrevUVs);
			result = mResources.mCur->mDenoiseResult;

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
			commandBuffer->fillBuffer(**mResources.mCur->mTonemapMax.buffer(), mResources.mCur->mTonemapMax.offset(), mResources.mCur->mTonemapMax.size_bytes(), 0);
			commandBuffer.barrier({mResources.mCur->mTonemapMax}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			mTonemapMaxReducePipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapMaxReducePipeline->descriptor("gAlbedo") = image_descriptor(mResources.mCur->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTonemapMaxReducePipeline->descriptor("gMax") = mResources.mCur->mTonemapMax;
			commandBuffer.bind_pipeline(mTonemapMaxReducePipeline->get_pipeline());
			mTonemapMaxReducePipeline->bind_descriptor_sets(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		result.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mResources.mCur->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mResources.mCur->mTonemapResult.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.barrier({mResources.mCur->mTonemapMax}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite,  vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

		mTonemapPipeline->descriptor("gInput")  = image_descriptor(result, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(mResources.mCur->mTonemapResult, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mResources.mCur->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gMax") = mResources.mCur->mTonemapMax;
		mTonemapPipeline->descriptor("gPrevMax") = mResources.mPrev && mResources.mPrev->mTonemapMax ? mResources.mPrev->mTonemapMax : mResources.mCur->mTonemapMax;
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	if (mResources.mCur->mTonemapResult.image()->format() == renderTarget.image()->format())
		commandBuffer.copy_image(mResources.mCur->mTonemapResult, renderTarget);
	else
		commandBuffer.blit_image(mResources.mCur->mTonemapResult, renderTarget);

	// copy selection data
	{
		Buffer::View<VisibilityInfo> v = mResources.mCur->mPathData.at("gVisibility").cast<VisibilityInfo>();
		commandBuffer.barrier({ v }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		const float2 c = commandBuffer.mDevice.mInstance.window().input_state().cursor_pos();
		const int2 cp = int2((int)c.x(), (int)c.y());
		mResources.mCur->mSelectionDataValid = false;
		for (const auto&[view, transform] : views)
			if (view.test_inside(cp)) {
				commandBuffer.copy_buffer(Buffer::View<VisibilityInfo>(v, cp.y() * view.extent().x() + cp.x(), 1), mResources.mCur->mSelectionData);
				mResources.mCur->mSelectionDataValid = true;
			}
	}

	commandBuffer.write_timestamp(vk::PipelineStageFlagBits::eTopOfPipe, "VCM::render done");
}

}