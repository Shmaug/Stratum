#include "BDPT.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

#include <Shaders/tonemap.h>

namespace stm {

#pragma pack(push)
#pragma pack(1)
#include <Shaders/reservoir.h>
#pragma pack(pop)

void inspector_gui_fn(BDPT* v) { v->on_inspector_gui(); }

BDPT::BDPT(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);
	app->OnUpdate.listen(mNode, bind(&BDPT::update, this, std::placeholders::_1, std::placeholders::_2), EventPriority::eAlmostLast);

#ifdef STRATUM_ENABLE_OPENXR
	auto xrnode = app.node().find_in_descendants<XR>();
	if (xrnode) {
		xrnode->OnRender.listen(mNode, [&, app](CommandBuffer& commandBuffer) {
			vector<pair<ViewData,TransformData>> views;
			views.reserve(xrnode->views().size());
			for (const XR::View& v : xrnode->views())
				views.emplace_back(v.mCamera.view(), node_to_world(v.mCamera.node()));
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
			render(commandBuffer, app->window().back_buffer(), { { scene->mMainCamera->view(), node_to_world(scene->mMainCamera.node()) } });
		});
	}

	create_pipelines();
}

void BDPT::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	float exposure = 1;
	uint32_t tonemapper = 0;
	if (mTraceStepPipeline) {
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		tonemapper = mTonemapPipeline->specialization_constant<uint32_t>("gMode");
	} else {
		mPushConstants.gMinPathVertices = 3;
		mPushConstants.gMaxPathVertices = 8;
		mPushConstants.gMaxLightPathVertices = 4;
		mPushConstants.gMaxNullCollisions = 64;
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
	/*
	process_shader(mSamplePhotonsPipeline, "Shaders/bdpt_sample_photons.spv");
	process_shader(mVisibilityPipeline, "Shaders/bdpt_visibility.spv");
	process_shader(mTraceStepPipeline , "Shaders/bdpt_path_step.spv");
	/*/
	process_shader(mSamplePhotonsPipeline, "../../src/Shaders/kernels/renderers/bdpt.hlsl", "sample_photons", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	process_shader(mVisibilityPipeline, "../../src/Shaders/kernels/renderers/bdpt.hlsl", "visibility", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	process_shader(mTraceStepPipeline , "../../src/Shaders/kernels/renderers/bdpt.hlsl", "path_step", { "-matrix-layout-row-major", "-capability", "spirv_1_5", "-capability", "GL_EXT_ray_tracing" });
	//*/
	for (uint32_t i = 0; i < 2; i++)
		mDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "bdpt_descriptor_set_layout" + to_string(i), bindings[i]);

	mTonemapPipeline = make_shared<ComputePipelineState>("tonemap", make_shared<Shader>(instance->device(), "Shaders/tonemap.spv"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;
	mTonemapPipeline->specialization_constant<uint32_t>("gMode") = tonemapper;

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

	ImGui::CheckboxFlags("Count Rays/second", &mSamplingFlags, BDPT_FLAG_COUNT_RAYS);
	if (mSamplingFlags & BDPT_FLAG_COUNT_RAYS) {
		const auto [rps, ext] = format_number(mRaysPerSecond);
		ImGui::Text("%.2f%s Rays/second", rps, ext);
	}

	if (ImGui::BeginCombo("Debug Mode", to_string(mDebugMode).c_str())) {
		for (uint32_t i = 0; i < (uint32_t)DebugMode::eDebugModeCount; i++)
			if (ImGui::Selectable(to_string((DebugMode)i).c_str(), mDebugMode == (DebugMode)i)) {
				mDebugMode = (DebugMode)i;
			}
		ImGui::EndCombo();
	}
	if (mDebugMode == DebugMode::ePathLengthContribution) {
		ImGui::Indent();
		ImGui::DragScalar("View Path Length", ImGuiDataType_U32, &mPushConstants.gDebugViewPathLength, 1);
		ImGui::DragScalar("Light Path Length", ImGuiDataType_U32, &mPushConstants.gDebugLightPathLength, 1);
		ImGui::Unindent();
	}

	if (ImGui::CollapsingHeader("Path Tracing")) {
		auto define_checkbox = [&](const auto& pipeline, const char* label, const string& define) {
			bool v = pipeline->has_specialization_constant(define);
			if (ImGui::Checkbox(label, &v)) {
				if (v) pipeline->specialization_constant<string>(define) = "";
				else pipeline->erase_specialization_constant(define);
			}
		};
		define_checkbox(mTraceStepPipeline, "Single Bounce Kernel", "BDPT_SINGLE_BOUNCE_KERNEL");
		define_checkbox(mTraceStepPipeline, "Store beta in smem", "GROUPSHARED_BETA");
		define_checkbox(mTraceStepPipeline, "Store intersection in smem", "GROUPSHARED_ISECT");
		define_checkbox(mTraceStepPipeline, "Store material in smem", "GROUPSHARED_MATERIAL");
		define_checkbox(mTraceStepPipeline, "Store transmittance estimate in smem", "GROUPSHARED_TRANSMITTANCE");

		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Remap Threads", &mSamplingFlags, BDPT_FLAG_REMAP_THREADS);
		ImGui::CheckboxFlags("Demodulate Albedo", &mSamplingFlags, BDPT_FLAG_DEMODULATE_ALBEDO);
		ImGui::CheckboxFlags("Ray Cone LoD", &mSamplingFlags, BDPT_FLAG_RAY_CONES);

		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Path Vertices", ImGuiDataType_U32, &mPushConstants.gMaxPathVertices, 1);
		ImGui::DragScalar("Min Path Vertices", ImGuiDataType_U32, &mPushConstants.gMinPathVertices, 1);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPushConstants.gMaxNullCollisions, 1);
		ImGui::PopItemWidth();

		ImGui::CheckboxFlags("Connect Light Paths to Views", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_VIEWS);
		ImGui::CheckboxFlags("Connect Light Paths to View Paths", &mSamplingFlags, BDPT_FLAG_CONNECT_TO_LIGHT_PATHS);
		if (mSamplingFlags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
			uint32_t mn = 1;
			ImGui::DragScalar("Max Light Path Length", ImGuiDataType_U32, &mPushConstants.gMaxLightPathVertices, 1, &mn);
		}

		ImGui::CheckboxFlags("NEE", &mSamplingFlags, BDPT_FLAG_NEE);
		if (mSamplingFlags & BDPT_FLAG_NEE) {
			if (mPushConstants.gEnvironmentMaterialAddress != -1 && mPushConstants.gLightCount > 0) {
				ImGui::PushItemWidth(40);
				ImGui::Indent();
				ImGui::DragFloat("Environment Sample Probability", &mPushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
				ImGui::Unindent();
				ImGui::PopItemWidth();
			}
			ImGui::Indent();
			ImGui::CheckboxFlags("Multiple Importance", &mSamplingFlags, BDPT_FLAG_NEE_MIS);
			if (mPushConstants.gLightCount > 0) {
				ImGui::CheckboxFlags("Sample Light Power", &mSamplingFlags, BDPT_FLAG_SAMPLE_LIGHT_POWER);
				ImGui::CheckboxFlags("Uniform Sphere Sampling", &mSamplingFlags, BDPT_FLAG_UNIFORM_SPHERE_SAMPLING);
			}
			ImGui::Unindent();

			ImGui::CheckboxFlags("NEE Reservoir Sampling", &mSamplingFlags, BDPT_FLAG_RESERVOIR_NEE);
			if (mSamplingFlags & BDPT_FLAG_RESERVOIR_NEE) {
				ImGui::Indent();
				ImGui::DragScalar("Reservoir Samples", ImGuiDataType_U32, &mPushConstants.gNEEReservoirSamples, 1);
				ImGui::Unindent();
			}
		}
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		if (auto denoiser = mNode.find<Denoiser>(); denoiser)
			if (ImGui::Checkbox("Use Denoiser", &mDenoise))
				denoiser->reset_accumulation();
		uint32_t m = mTonemapPipeline->specialization_constant<uint32_t>("gMode");
		if (ImGui::BeginCombo("Tone mapping", to_string((TonemapMode)m).c_str())) {
			for (uint32_t i = 0; i < (uint32_t)TonemapMode::eTonemapModeCount; i++)
				if (ImGui::Selectable(to_string((TonemapMode)i).c_str(), m == i))
					mTonemapPipeline->specialization_constant<uint32_t>("gMode") = i;
			ImGui::EndCombo();
		}
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::PopItemWidth();
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant<uint32_t>("gGammaCorrection")));
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
	ProfilerRegion ps("BDPT::update", commandBuffer);

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

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mRayCount[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mRayCount[0];
		mRaysPerSecondTimer = 0;
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

		mCurFrame->mRadiance 		= make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAlbedo 			= make_shared<Image>(commandBuffer.mDevice, "gAlbedo", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mTonemapResult 	= make_shared<Image>(commandBuffer.mDevice, "gOutput", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDebugImage 		= make_shared<Image>(commandBuffer.mDevice, "gDebugImage", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);

		mCurFrame->mPathData = {
			{ "gVisibility", 		make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", 		   extent.width * extent.height * sizeof(VisibilityInfo), 	vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gPathStates", 		make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", 		   extent.width * extent.height * sizeof(PathState), 			vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gRayDifferentials", 	make_shared<Buffer>(commandBuffer.mDevice, "gRayDifferentials",    extent.width * extent.height * sizeof(RayDifferential), 	vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gReservoirs", 		make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", 		   extent.width * extent.height * sizeof(Reservoir), 				vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gLightTraceSamples", make_shared<Buffer>(commandBuffer.mDevice, "gLightTraceSamples",   extent.width * extent.height * sizeof(float4), 			vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gLightPathVertices0", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices0", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex0), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gLightPathVertices1", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices1", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex1), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gLightPathVertices2", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices2", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex2), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
			{ "gLightPathVertices3", make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices3", extent.width * extent.height * max(mPushConstants.gMaxLightPathVertices,1u) * sizeof(LightPathVertex3), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32) },
		};
		mCurFrame->mFrameNumber = 0;

		commandBuffer->fillBuffer(**mCurFrame->mPathData.at("gReservoirs").buffer(), mCurFrame->mPathData.at("gReservoirs").offset(), mCurFrame->mPathData.at("gReservoirs").size_bytes(), 0);
		commandBuffer.barrier({mCurFrame->mPathData.at("gReservoirs")}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
	}

	if (mCurFrame->mPathData.at("gLightPathVertices0").size_bytes() < extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex0)) {
		mCurFrame->mPathData["gLightPathVertices0"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices0", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex0), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices1"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices1", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex1), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices2"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices2", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex2), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
		mCurFrame->mPathData["gLightPathVertices3"] = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices3", extent.width * extent.height * mPushConstants.gMaxLightPathVertices * sizeof(LightPathVertex3), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_ONLY, 32);
	}

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

	if (push_constants.gLightCount == 0 && push_constants.gEnvironmentMaterialAddress == -1)
		sampling_flags &= ~BDPT_FLAG_NEE;

	if (has_volumes)
		sampling_flags |= BDPT_FLAG_HAS_MEDIA;
	else {
		push_constants.gMaxNullCollisions = 0;
		sampling_flags &= ~BDPT_FLAG_HAS_MEDIA;
	}

	mSamplePhotonsPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags | BDPT_FLAG_TRACE_LIGHT;
	mVisibilityPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
	mTraceStepPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
	mVisibilityPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;
	mTraceStepPipeline->specialization_constant<uint32_t>("gDebugMode") = (uint32_t)mDebugMode;

	mCurFrame->mViewDescriptors = make_shared<DescriptorSet>(mDescriptorSetLayouts[1], "bdpt_view_descriptors");
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViews"), mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewTransforms"), mCurFrame->mViewTransforms);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gInverseViewTransforms"), mCurFrame->mViewInverseTransforms);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevViews"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mViews : mCurFrame->mViews);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gPrevInverseViewTransforms"), (mPrevFrame && mPrevFrame->mViews) ? mPrevFrame->mViewInverseTransforms : mCurFrame->mViewInverseTransforms);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gViewMediumInstances"), mCurFrame->mViewMediumIndices);
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	mCurFrame->mViewDescriptors->insert_or_assign(mDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
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
	mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
	mCurFrame->mDebugImage.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

	// trace light paths
	commandBuffer->fillBuffer(**mCurFrame->mPathData.at("gLightTraceSamples").buffer(), mCurFrame->mPathData.at("gLightTraceSamples").offset(), mCurFrame->mPathData.at("gLightTraceSamples").size_bytes(), 0);
	commandBuffer.barrier({mCurFrame->mPathData.at("gLightTraceSamples")}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
	if (sampling_flags & (BDPT_FLAG_CONNECT_TO_VIEWS|BDPT_FLAG_CONNECT_TO_LIGHT_PATHS)) {
		{
			ProfilerRegion ps("Sample photons", commandBuffer);
			commandBuffer.bind_pipeline(mSamplePhotonsPipeline->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
		}

		if (push_constants.gMaxLightPathVertices > 2) { // Trace paths
			ProfilerRegion ps("Trace light paths", commandBuffer);
			mTraceStepPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags | BDPT_FLAG_TRACE_LIGHT;
			commandBuffer.bind_pipeline(mTraceStepPipeline->get_pipeline(mDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			#ifdef BDPT_SINGLE_BOUNCE_KERNEL
			const uint32_t n = push_constants.gMaxLightPathVertices;
			#else
			const uint32_t n = mTraceStepPipeline->has_specialization_constant("BDPT_SINGLE_BOUNCE_KERNEL") ? push_constants.gMaxLightPathVertices : 3;
			#endif
			for (uint i = 2; i < n; i++) {
				for (const auto&[name, buf] : mCurFrame->mPathData)
					commandBuffer.barrier({buf}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.dispatch_over(extent);
			}
			mTraceStepPipeline->specialization_constant<uint32_t>("gSpecializationFlags") = sampling_flags;
		}

		for (const auto&[name, buf] : mCurFrame->mPathData)
			commandBuffer.barrier({buf}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
	}

	{ // Visibility
		ProfilerRegion ps("Sample visibility", commandBuffer);
		commandBuffer.bind_pipeline(mVisibilityPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	if (push_constants.gMaxPathVertices > 2) { // Trace paths
		ProfilerRegion ps("Trace view paths", commandBuffer);
		commandBuffer.bind_pipeline(mTraceStepPipeline->get_pipeline(mDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		#ifdef BDPT_SINGLE_BOUNCE_KERNEL
		const uint32_t n = push_constants.gMaxPathVertices;
		#else
		const uint32_t n = mTraceStepPipeline->has_specialization_constant("BDPT_SINGLE_BOUNCE_KERNEL") ? push_constants.gMaxPathVertices : 3;
		#endif
		for (uint i = 2; i < n; i++) {
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
			for (const auto&[name, buf] : mCurFrame->mPathData)
				commandBuffer.barrier({buf}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.dispatch_over(extent);
		}
	}

	Image::View tonemap_in = mCurFrame->mRadiance;
	if (mDebugMode != DebugMode::eNone)
		tonemap_in = mCurFrame->mDebugImage;

	if (mDenoise) {
		auto denoiser = mNode.find<Denoiser>();
		if (denoiser) {
			if (mPrevFrame && mPrevFrame->mViewTransforms && (mCurFrame->mViewTransforms[0].m != mPrevFrame->mViewTransforms[0].m).any())
				denoiser->reset_accumulation();
			mCurFrame->mDenoiseResult = denoiser->denoise(commandBuffer, tonemap_in, mCurFrame->mViews, mCurFrame->mPathData.at("gVisibility").cast<VisibilityInfo>());
			tonemap_in = mCurFrame->mDenoiseResult;
		}
	}

	{
		ProfilerRegion ps("Tonemap", commandBuffer);
		tonemap_in.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mTonemapResult.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->specialization_constant<uint32_t>("gModulateAlbedo") = (bool)(sampling_flags & BDPT_FLAG_DEMODULATE_ALBEDO);
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