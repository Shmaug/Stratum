#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <stb_image_write.h>

#include <random>

using namespace stm::hlsl;

namespace stm {

namespace hlsl {
#include <HLSL/kernels/a-svgf/filter_type.hlsli>
#include <HLSL/tonemap.hlsli>
}

void inspector_gui_fn(RayTraceScene* v) { v->on_inspector_gui(); }

RayTraceScene::RayTraceScene(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn(&inspector_gui_fn);

	create_pipelines();
}

void RayTraceScene::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	auto samplerClamp = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	uint32_t debugMode;
	float exposure;
	optional<uint32_t> samplingFlags;
	if (mRandomWalkPipeline) {
		// pipelines already exist, keep track of old inputs
		debugMode = mTonemapPipeline->specialization_constant("gDebugMode");
		exposure = mTonemapPipeline->push_constant<float>("gExposure");
		samplingFlags = mRandomWalkPipeline->specialization_constant("gSamplingFlags");
		mNode.node_graph().erase_recurse(*mRandomWalkPipeline.node().parent());
	} else {
		debugMode = 0;
		exposure = 1;
		mPathTracePushConstants.gMaxEyeDepth = 8;
		mPathTracePushConstants.gMaxLightDepth = 0;
		mPathTracePushConstants.gMinDepth = 1;
		mPathTracePushConstants.gReservoirSamples = 1;
		mPathTracePushConstants.gMaxNullCollisions = 64;
	}

	Node& n = mNode.make_child("pipelines");

	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();

	mCopyVerticesPipeline = n.make_child("copy_vertices").make_component<ComputePipelineState>("copy_vertices", shaders.at("copy_vertices"));

	unordered_map<uint32_t, DescriptorSetLayout::Binding> bindings[2];
	auto make_binding_fn = [&](const string& name, const ShaderModule::DescriptorBinding& binding) {
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
		mPathTraceDescriptorMap[binding.mSet].emplace(name, binding.mBinding);
	};
	for (const auto[name,binding] : shaders.at("pt_sample_photons")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_sample_visibility")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_random_walk")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_resolve")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("gradient_forward_project")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("create_gradient_samples")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("atrous_gradient")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("temporal_accumulation")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("estimate_variance")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("atrous")->descriptors()) make_binding_fn(name, binding);
	for (uint32_t i = 0; i < 2; i++)
		mPathTraceDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "path_trace_descriptor_set_layout" + to_string(i), bindings[i]);

	mSamplePhotonsPipeline = n.make_child("pt_sample_photons").make_component<ComputePipelineState>("pt_sample_photons", shaders.at("pt_sample_photons"));
	mSampleVisibilityPipeline = n.make_child("pt_sample_visibility").make_component<ComputePipelineState>("pt_sample_visibility", shaders.at("pt_sample_visibility"));
	mRandomWalkPipeline = n.make_child("pt_random_walk").make_component<ComputePipelineState>("pt_random_walk", shaders.at("pt_random_walk"));
	if (samplingFlags) mRandomWalkPipeline->specialization_constant("gSamplingFlags") = *samplingFlags;

	mResolvePipeline = n.make_child("pt_resolve").make_component<ComputePipelineState>("pt_resolve", shaders.at("pt_resolve"));

	mGradientForwardProjectPipeline = n.make_child("gradient_forward_project").make_component<ComputePipelineState>("gradient_forward_project", shaders.at("gradient_forward_project"));
	mCreateGradientSamplesPipeline = n.make_child("create_gradient_samples").make_component<ComputePipelineState>("create_gradient_samples", shaders.at("create_gradient_samples"));
	mAtrousGradientPipeline = n.make_child("atrous_gradient").make_component<ComputePipelineState>("atrous_gradient", shaders.at("atrous_gradient"));
	mAtrousGradientPipeline->push_constant<float>("gSigmaLuminanceBoost") = 1;
	mTemporalAccumulationPipeline = n.make_child("temporal_accumulation").make_component<ComputePipelineState>("temporal_accumulation", shaders.at("temporal_accumulation"));
	mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit") = 128;
	mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") = 0;
	mTemporalAccumulationPipeline->push_constant<uint32_t>("gGradientDownsample") = 3;
	mTemporalAccumulationPipeline->set_immutable_sampler("gSampler", samplerClamp);
	mEstimateVariancePipeline = n.make_child("estimate_variance").make_component<ComputePipelineState>("estimate_variance", shaders.at("estimate_variance"));
	mAtrousPipeline = n.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
	mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost") = 3;
	mCopyRGBPipeline = n.make_child("copy_rgb").make_component<ComputePipelineState>("atrous_copy_rgb", shaders.at("atrous_copy_rgb"));

	mTonemapPipeline = n.make_child("tonemap").make_component<ComputePipelineState>("tonemap", shaders.at("tonemap"));
	mTonemapPipeline->push_constant<float>("gExposure") = exposure;
	mTonemapPipeline->specialization_constant("gDebugMode") = debugMode;

	mCounterValues = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	memset(mCounterValues.data(), 0, mCounterValues.size_bytes());
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;
}

void RayTraceScene::on_inspector_gui() {
	const auto[rps, ext] = format_number(mRaysPerSecond);
	ImGui::Text("%.2f%s Rays/second", rps, ext);
	if (mCurFrame) {
		ImGui::Text("%lu instances", mCurFrame->mInstances.size());
		ImGui::Text("%lu lights", mCurFrame->mLightInstances.size());
		ImGui::Text("%u materials", mCurFrame->mMaterialCount);
	}

	ImGui::Checkbox("Update Scene Every Frame", &mUpdateSceneEachFrame);

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Eye Depth", ImGuiDataType_U32, &mPathTracePushConstants.gMaxEyeDepth, 1);
		ImGui::DragScalar("Min Depth", ImGuiDataType_U32, &mPathTracePushConstants.gMinDepth, 1);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPathTracePushConstants.gMaxNullCollisions, 1);
		ImGui::PopItemWidth();

		ImGui::CheckboxFlags("Sample Pixel Area", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_PIXEL_AREA);
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Sample Emissives", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_EMISSIVE);
		ImGui::CheckboxFlags("Sample Environment", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_ENVIRONMENT);
		if (mPathTracePushConstants.gEnvironmentMaterialAddress != -1 && mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_ENVIRONMENT) {
			ImGui::PushItemWidth(40);
			ImGui::Indent();
			ImGui::DragFloat("Environment Sample Probability", &mPathTracePushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
			ImGui::Unindent();
			ImGui::PopItemWidth();
		}
		if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & (SAMPLE_FLAG_SAMPLE_EMISSIVE | SAMPLE_FLAG_SAMPLE_ENVIRONMENT)) {
			ImGui::Indent();
			ImGui::CheckboxFlags("Multiple Importance", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_MIS);
			if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_EMISSIVE)
				ImGui::CheckboxFlags("Uniform Sphere Sampling", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_UNIFORM_SPHERE_SAMPLING);

			ImGui::PushItemWidth(40);
			ImGui::DragScalar("Max Light Path Length", ImGuiDataType_U32, &mPathTracePushConstants.gMaxLightDepth, 1);
			ImGui::PopItemWidth();
			if (mPathTracePushConstants.gMaxLightDepth > 0)
				ImGui::CheckboxFlags("Randomly Sample Light Paths", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_RANDOM_LIGHT_PATHS);
			ImGui::CheckboxFlags("Reservoir Sampling", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_SAMPLE_RESERVOIRS);
			if (mRandomWalkPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_SAMPLE_RESERVOIRS) {
				ImGui::Indent();
				ImGui::PushItemWidth(40);
				uint32_t one = 1;
				ImGui::DragScalar("Reservoir Samples", ImGuiDataType_U32, &mPathTracePushConstants.gReservoirSamples, 1, &one);
				ImGui::PopItemWidth();
				ImGui::Unindent();
			}
			ImGui::Unindent();
		}
		ImGui::CheckboxFlags("Ray Cone LoD", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_RAY_CONE_LOD);
		ImGui::CheckboxFlags("Demodulate Albedo", &mRandomWalkPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_DEMODULATE_ALBEDO);
	}

	if (ImGui::CollapsingHeader("Denoising")) {
		ImGui::Checkbox("Reprojection", &mReprojection);
		if (mReprojection) {
			if (ImGui::Checkbox("Use Visibility", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gUseVisibility")))) {
				mEstimateVariancePipeline->specialization_constant("gUseVisibility") = mTemporalAccumulationPipeline->specialization_constant("gUseVisibility");
				mAtrousPipeline->specialization_constant("gUseVisibility") = mTemporalAccumulationPipeline->specialization_constant("gUseVisibility");
			}
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

			if (ImGui::DragFloat("Antilag Scale", &mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"), .01f, 0, 0, "%.1f"))
					mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") = max(mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"), 0.f);
			if (mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") > 0) {
				ImGui::Indent();
				ImGui::InputScalar("Antilag Radius", ImGuiDataType_U32, &mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius"));
				if (ImGui::DragScalar("Gradient Downsample", ImGuiDataType_U32, &mCreateGradientSamplesPipeline->specialization_constant("gGradientDownsample"), 0.1f)) {
					const uint32_t& gGradientDownsample = max(1u, min(7u, mCreateGradientSamplesPipeline->specialization_constant("gGradientDownsample")));
					mGradientForwardProjectPipeline->specialization_constant("gGradientDownsample") = gGradientDownsample;
					mCreateGradientSamplesPipeline->specialization_constant("gGradientDownsample") = gGradientDownsample;
					mTemporalAccumulationPipeline->push_constant<uint32_t>("gGradientDownsample") = gGradientDownsample;
					mAtrousGradientPipeline->specialization_constant("gGradientDownsample") = gGradientDownsample;
					mTonemapPipeline->specialization_constant("gGradientDownsample") = gGradientDownsample;
				}
				ImGui::DragScalar("Gradient Filter Iterations", ImGuiDataType_U32, &mDiffAtrousIterations, 0.1f);
				if (mDiffAtrousIterations > 0) {
					ImGui::DragFloat("Gradient Sigma Luminance Boost", &mAtrousGradientPipeline->push_constant<float>("gSigmaLuminanceBoost"), .1f, 0, 0, "%.2f");
					ImGui::PopItemWidth();
					const uint32_t m = mAtrousGradientPipeline->specialization_constant("gFilterKernelType");
					if (ImGui::BeginCombo("Gradient Filter Type", to_string((FilterKernelType)m).c_str())) {
						for (uint32_t i = 0; i < FilterKernelType::eFilterKernelTypeCount; i++)
							if (ImGui::Selectable(to_string((FilterKernelType)i).c_str(), m == i))
								mAtrousGradientPipeline->specialization_constant("gFilterKernelType") = i;
						ImGui::EndCombo();
					}
					ImGui::PushItemWidth(40);
				}

				ImGui::Unindent();
			}
			ImGui::PopItemWidth();
		}
	}

	if (ImGui::CollapsingHeader("Post Processing")) {
		uint32_t m = mTonemapPipeline->specialization_constant("gMode");
		if (ImGui::BeginCombo("Tone mapping", to_string((TonemapMode)m).c_str())) {
			for (uint32_t i = 0; i < TonemapMode::eTonemapModeCount; i++)
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
		static char path[256] { 'i', 'm', 'a', 'g', 'e', '.', 'h', 'd', 'r', '\0' };
		ImGui::InputText("Output HDR", path, sizeof(path));
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			Device& d = mPrevFrame->mRadiance.image()->mDevice;
			auto cb = d.get_command_buffer("image copy");

			Image::View src = mReprojection ? mPrevFrame->mAccumColor : mPrevFrame->mRadiance;
			if (src.image()->format() != vk::Format::eR32G32B32A32Sfloat) {
				Image::View tmp = make_shared<Image>(cb->mDevice, "gRadiance", src.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eStorage);
				cb->blit_image(src, tmp);
				src = tmp;
			}

			Buffer::View<float> pixels = make_shared<Buffer>(d, "image copy tmp", src.extent().width*src.extent().height*sizeof(float)*4, vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
			cb->copy_image_to_buffer(src, pixels);

			d->waitIdle();
			d.submit(cb);
			cb->fence()->wait();

			stbi_write_hdr(path, src.extent().width, src.extent().height, 4, pixels.data());
		}
	}

	uint32_t m = mTonemapPipeline->specialization_constant("gDebugMode");
  	if (ImGui::BeginCombo("Debug Mode", to_string((DebugMode)m).c_str())) {
    for (uint32_t i = 0; i < DebugMode::eDebugModeCount; i++)
      if (ImGui::Selectable(to_string((DebugMode)i).c_str(), m == i)) {
        mTonemapPipeline->specialization_constant("gDebugMode") = i;
		mSampleVisibilityPipeline->specialization_constant("gDebugMode") = i;
	  }
    ImGui::EndCombo();
	}
}

void RayTraceScene::update_scene(CommandBuffer& commandBuffer, const float deltaTime) {
	uint32_t totalVertexCount = 0;
	uint32_t totalIndexBufferSize = 0;

	ByteAppendBuffer materialData;
	materialData.data.reserve(mPrevFrame && mPrevFrame->mMaterialData ? mPrevFrame->mMaterialData.size()/sizeof(uint32_t) : 1);
	unordered_map<Material*, uint32_t> materialMap;

	vector<tuple<MeshPrimitive*, MeshAS*, uint32_t>> instanceIndices;
	vector<vk::AccelerationStructureInstanceKHR> instancesAS;
	vector<InstanceData> instanceDatas;
	if (mPrevFrame) instanceDatas.reserve(mPrevFrame->mInstances.size());
	vector<uint32_t> lightInstances;
	lightInstances.reserve(1);

	mCurFrame->mInstanceIndexMap = make_shared<Buffer>(commandBuffer.mDevice, "InstanceIndexMap", sizeof(uint32_t)*max<size_t>(1, mPrevFrame ? mPrevFrame->mInstances.size() : 0), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mCurFrame->mInstanceIndexMap, -1);

	vector<vk::BufferMemoryBarrier> blasBarriers;
	mCurFrame->mMaterialCount = 0;

	{ // spheres
		ProfilerRegion s("Process spheres", commandBuffer);
		mNode.for_each_descendant<SpherePrimitive>([&](const component_ptr<SpherePrimitive>& prim) {
			// append unique materials to materials list
			auto materialMap_it = materialMap.find(prim->mMaterial.get());
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *prim->mMaterial);
				mCurFrame->mMaterialCount++;
			}
			if (prim->mMaterial->index() == BSDFType::eEmissive)
				lightInstances.emplace_back((uint32_t)instanceDatas.size());

			TransformData transform = node_to_world(prim.node());
			float r = prim->mRadius;
			#ifdef TRANSFORM_UNIFORM_SCALING
			r *= transform.mScale;
			transform = make_transform(transform.mPosition, quatf_identity(), float3::Ones());
			#else
			r *= transform.m.block<3,3>(0,0).matrix().determinant();
			transform = make_transform(transform.m.col(3).head<3>(), quatf_identity(), float3::Ones());
			#endif

			const float3 mn = -float3::Constant(r);
			const float3 mx = float3::Constant(r);
			const size_t key = hash_args(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
			auto aabb_it = mAABBs.find(key);
			if (aabb_it == mAABBs.end()) {
				Buffer::View<vk::AabbPositionsKHR> aabb = make_shared<Buffer>(commandBuffer.mDevice, "aabb data", sizeof(vk::AabbPositionsKHR), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU);
				aabb[0].minX = mn[0];
				aabb[0].minY = mn[1];
				aabb[0].minZ = mn[2];
				aabb[0].maxX = mx[0];
				aabb[0].maxY = mx[1];
				aabb[0].maxZ = mx[2];
				vk::AccelerationStructureGeometryAabbsDataKHR aabbs(commandBuffer.hold_resource(aabb).device_address(), sizeof(vk::AabbPositionsKHR));
				vk::AccelerationStructureGeometryKHR aabbGeometry(vk::GeometryTypeKHR::eAabbs, aabbs, vk::GeometryFlagBitsKHR::eOpaque);
				vk::AccelerationStructureBuildRangeInfoKHR range(1);
				shared_ptr<AccelerationStructure> as = make_shared<AccelerationStructure>(commandBuffer, "aabb BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, aabbGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());
				aabb_it = mAABBs.emplace(key, as).first;
			}

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_SPHERES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);

			TransformData prevTransform;
			if (mPrevFrame)
				if (auto it = mPrevFrame->mInstanceTransformMap.find(prim.get()); it != mPrevFrame->mInstanceTransformMap.end()) {
					prevTransform = it->second.first;
					mCurFrame->mInstanceIndexMap[it->second.second] = instance.instanceCustomIndex;
				}

			mCurFrame->mInstanceTransformMap.emplace(prim.get(), make_pair(transform, (uint32_t)instance.instanceCustomIndex));
			instanceDatas.emplace_back( make_instance_sphere(transform, prevTransform, materialMap_it->second, r) );
		});
	}

	{ // meshes
		Material error_mat = Lambertian{};
		get<Lambertian>(error_mat).reflectance = make_image_value3({}, float3(1,0,1));
		materialMap.emplace(nullptr, (uint32_t)(materialData.data.size()*sizeof(uint32_t)));
		store_material(materialData, mCurFrame->mResources, error_mat);

		ProfilerRegion s("Process meshes", commandBuffer);
		mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
			if (prim->mMesh->topology() != vk::PrimitiveTopology::eTriangleList) return;
			if (!prim->mMaterial) return;

			// build BLAS
			auto it = mMeshAccelerationStructures.find(prim->mMesh.get());
			if (it == mMeshAccelerationStructures.end()) {
				ProfilerRegion s("build acceleration structures", commandBuffer);
				const auto& [vertexPosDesc, positions] = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];

				if (prim->mMesh->index_type() != vk::IndexType::eUint32 && prim->mMesh->index_type() != vk::IndexType::eUint16)
					return;

				vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
				triangles.vertexFormat = vertexPosDesc.mFormat;
				triangles.vertexData = commandBuffer.hold_resource(positions).device_address();
				triangles.vertexStride = vertexPosDesc.mStride;
				triangles.maxVertex = (uint32_t)(positions.size_bytes()/vertexPosDesc.mStride);
				triangles.indexType = prim->mMesh->index_type();
				triangles.indexData = commandBuffer.hold_resource(prim->mMesh->indices()).device_address();
				vk::GeometryFlagBitsKHR flag = vk::GeometryFlagBitsKHR::eOpaque;
				// TODO: non-opaque geometry
				vk::AccelerationStructureGeometryKHR triangleGeometry(vk::GeometryTypeKHR::eTriangles, triangles, flag);
				vk::AccelerationStructureBuildRangeInfoKHR range(prim->mMesh->indices().size()/(prim->mMesh->indices().stride()*3));
				auto as = make_shared<AccelerationStructure>(commandBuffer, prim.node().name()+"/BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, triangleGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());

				if (mMeshVertices.find(prim->mMesh.get()) == mMeshVertices.end()) {
					Buffer::View<PackedVertexData>& vertices = mMeshVertices.emplace(prim->mMesh.get(),
						make_shared<Buffer>(commandBuffer.mDevice, prim.node().name()+"/PackedVertexData", triangles.maxVertex*sizeof(PackedVertexData), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eShaderDeviceAddress)).first->second;

					// copy vertex data
					auto positions = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];
					auto normals   = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eNormal)[0];
					auto texcoords = prim->mMesh->vertices()->find(VertexArrayObject::AttributeType::eTexcoord);
					auto tangents  = prim->mMesh->vertices()->find(VertexArrayObject::AttributeType::eTangent);

					commandBuffer.bind_pipeline(mCopyVerticesPipeline->get_pipeline());
					mCopyVerticesPipeline->descriptor("gVertices") = vertices;
					mCopyVerticesPipeline->descriptor("gPositions") = Buffer::View(positions.second, positions.first.mOffset);
					mCopyVerticesPipeline->descriptor("gNormals")   = Buffer::View(normals.second, normals.first.mOffset);
					mCopyVerticesPipeline->descriptor("gTangents")  = tangents ? Buffer::View(tangents->second, tangents->first.mOffset) : positions.second;
					mCopyVerticesPipeline->descriptor("gTexcoords") = texcoords ? Buffer::View(texcoords->second, texcoords->first.mOffset) : positions.second;
					mCopyVerticesPipeline->push_constant<uint32_t>("gCount") = vertices.size();
					mCopyVerticesPipeline->push_constant<uint32_t>("gPositionStride") = positions.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gNormalStride") = normals.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gTangentStride") = tangents ? tangents->first.mStride : 0;
					mCopyVerticesPipeline->push_constant<uint32_t>("gTexcoordStride") = texcoords ? texcoords->first.mStride : 0;
					mCopyVerticesPipeline->bind_descriptor_sets(commandBuffer);
					mCopyVerticesPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(triangles.maxVertex);
					commandBuffer.barrier({vertices}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
				}

				it = mMeshAccelerationStructures.emplace(prim->mMesh.get(), MeshAS { as, prim->mMesh->indices() }).first;
			}

			// append unique materials to materials list
			auto materialMap_it = materialMap.find(prim->mMaterial.get());
			if (materialMap_it == materialMap.end()) {
				ProfilerRegion s("store_material");
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *prim->mMaterial);
				mCurFrame->mMaterialCount++;
			}
			if (prim->mMaterial->index() == BSDFType::eEmissive)
				lightInstances.emplace_back((uint32_t)instanceDatas.size());

			TransformData transform;
			{
				ProfilerRegion s("node_to_world");
				transform = node_to_world(prim.node());
			}
			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_TRIANGLES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));

			const uint32_t triCount = prim->mMesh->indices().size_bytes() / (prim->mMesh->indices().stride()*3);

			TransformData prevTransform = transform;
			if (mPrevFrame)
				if (auto transform_it = mPrevFrame->mInstanceTransformMap.find(prim.get()); transform_it != mPrevFrame->mInstanceTransformMap.end()) {
					prevTransform = transform_it->second.first;
					mCurFrame->mInstanceIndexMap[transform_it->second.second] = instance.instanceCustomIndex;
				}
			{
				ProfilerRegion s("create and store instance data");
				instanceIndices.emplace_back(prim.get(), &it->second, (uint32_t)instance.instanceCustomIndex);
				mCurFrame->mInstanceTransformMap.emplace(prim.get(), make_pair(transform, (uint32_t)instance.instanceCustomIndex));
				instanceDatas.emplace_back(make_instance_triangles(transform, prevTransform, materialMap_it->second, triCount, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride()));
				totalVertexCount += mMeshVertices.at(prim->mMesh.get()).size();
				totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);
			}
		});
	}

	{ // volumes
		ProfilerRegion s("Process heterogeneous volumes", commandBuffer);
		mNode.for_each_descendant<HeterogeneousVolume>([&](const component_ptr<HeterogeneousVolume>& vol) {
			auto materialMap_it = materialMap.find(reinterpret_cast<Material*>(vol.get()));
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(reinterpret_cast<Material*>(vol.get()), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *vol);
				mCurFrame->mMaterialCount++;
			}

			auto density_grid = vol->density_grid->grid<float>();
			const nanovdb::Vec3R& mn = density_grid->worldBBox().min();
			const nanovdb::Vec3R& mx = density_grid->worldBBox().max();
			const size_t key = hash_args((float)mn[0], (float)mn[1], (float)mn[2], (float)mx[0], (float)mx[1], (float)mx[2]);
			auto aabb_it = mAABBs.find(key);
			if (aabb_it == mAABBs.end()) {
				Buffer::View<vk::AabbPositionsKHR> aabb = make_shared<Buffer>(commandBuffer.mDevice, "aabb data", sizeof(vk::AabbPositionsKHR), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU);
				aabb[0].minX = (float)mn[0];
				aabb[0].minY = (float)mn[1];
				aabb[0].minZ = (float)mn[2];
				aabb[0].maxX = (float)mx[0];
				aabb[0].maxY = (float)mx[1];
				aabb[0].maxZ = (float)mx[2];
				vk::AccelerationStructureGeometryAabbsDataKHR aabbs(commandBuffer.hold_resource(aabb).device_address(), sizeof(vk::AabbPositionsKHR));
				vk::AccelerationStructureGeometryKHR aabbGeometry(vk::GeometryTypeKHR::eAabbs, aabbs, vk::GeometryFlagBitsKHR::eOpaque);
				vk::AccelerationStructureBuildRangeInfoKHR range(1);
				shared_ptr<AccelerationStructure> as = make_shared<AccelerationStructure>(commandBuffer, "aabb BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, aabbGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());
				aabb_it = mAABBs.emplace(key, as).first;
			}

			const TransformData transform = node_to_world(vol.node());

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_VOLUME;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);

			TransformData prevTransform = transform;
			if (mPrevFrame)
				if (auto it = mPrevFrame->mInstanceTransformMap.find(vol.get()); it != mPrevFrame->mInstanceTransformMap.end()) {
					prevTransform = it->second.first;
					mCurFrame->mInstanceIndexMap[it->second.second] = instance.instanceCustomIndex;
				}

			mCurFrame->mInstanceTransformMap.emplace(vol.get(), make_pair(transform, (uint32_t)instance.instanceCustomIndex));
			instanceDatas.emplace_back( make_instance_volume(transform, prevTransform, materialMap_it->second, mCurFrame->mResources.volume_data_map.at(vol->density_buffer)) );
		});
	}

	{ // Build TLAS
		ProfilerRegion s("Build TLAS", commandBuffer);
		commandBuffer.barrier(blasBarriers, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);
		vk::AccelerationStructureGeometryKHR geom { vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryInstancesDataKHR() };
		vk::AccelerationStructureBuildRangeInfoKHR range { (uint32_t)instancesAS.size() };
		if (!instancesAS.empty()) {
			auto buf = make_shared<Buffer>(commandBuffer.mDevice, "TLAS instance buffer", sizeof(vk::AccelerationStructureInstanceKHR)*instancesAS.size(), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
			memcpy(buf->data(), instancesAS.data(), buf->size());
			commandBuffer.hold_resource(buf);
			geom.geometry.instances.data = buf->device_address();
		}
		mCurFrame->mScene = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geom, range);
		commandBuffer.barrier({commandBuffer.hold_resource(mCurFrame->mScene).buffer()},
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);
	}

	{ // environment map
		ProfilerRegion s("Process env map", commandBuffer);
		component_ptr<Material> envMap;
		mNode.for_each_descendant<Material>([&](component_ptr<Material> m) {
			if (m->index() == BSDFType::eEnvironment)
				envMap = m;
		});
		if (envMap) {
			uint32_t address = (uint32_t)(materialData.data.size()*sizeof(uint32_t));
			store_material(materialData, mCurFrame->mResources, *envMap);
			mCurFrame->mMaterialCount++;
			mPathTracePushConstants.gEnvironmentMaterialAddress = address;
			if (mPathTracePushConstants.gEnvironmentSampleProbability == 0)
				mPathTracePushConstants.gEnvironmentSampleProbability = 0.5f;
		} else {
			mPathTracePushConstants.gEnvironmentMaterialAddress = INVALID_MATERIAL;
			mPathTracePushConstants.gEnvironmentSampleProbability = 0;
		}
	}

	{ // copy vertices and indices

		ProfilerRegion s("Copy vertex data", commandBuffer);

		if (!mCurFrame->mVertices || mCurFrame->mVertices.size() < totalVertexCount)
			mCurFrame->mVertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(PackedVertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		if (!mCurFrame->mIndices || mCurFrame->mIndices.size() < totalIndexBufferSize)
			mCurFrame->mIndices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);

		for (uint32_t i = 0; i < instanceIndices.size(); i++) {
			const auto&[prim, blas, instanceIndex] = instanceIndices[i];
			const InstanceData& d = instanceDatas[instanceIndex];
			Buffer::View<PackedVertexData>& meshVertices = mMeshVertices.at(prim->mMesh.get());
			commandBuffer.copy_buffer(meshVertices, Buffer::View<PackedVertexData>(mCurFrame->mVertices.buffer(), d.first_vertex()*sizeof(PackedVertexData), meshVertices.size()));
			commandBuffer.copy_buffer(blas->mIndices, Buffer::View<byte>(mCurFrame->mIndices.buffer(), d.indices_byte_offset(), blas->mIndices.size_bytes()));
		}
		commandBuffer.barrier({ mCurFrame->mIndices, mCurFrame->mVertices }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}

	if (!mCurFrame->mInstances || mCurFrame->mInstances.size() < instanceDatas.size())
		mCurFrame->mInstances = make_shared<Buffer>(commandBuffer.mDevice, "gInstances", max<size_t>(1, instanceDatas.size())*sizeof(InstanceData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mMaterialData || mCurFrame->mMaterialData.size_bytes() < materialData.data.size()*sizeof(uint32_t))
		mCurFrame->mMaterialData = make_shared<Buffer>(commandBuffer.mDevice, "gMaterialData", max<size_t>(1, materialData.data.size())*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mLightInstances || mCurFrame->mLightInstances.size() < lightInstances.size())
		mCurFrame->mLightInstances = make_shared<Buffer>(commandBuffer.mDevice, "gLightInstances", max<size_t>(1, lightInstances.size())*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mDistributionData || mCurFrame->mDistributionData.size() < mCurFrame->mResources.distribution_data_size)
		mCurFrame->mDistributionData = make_shared<Buffer>(commandBuffer.mDevice, "gDistributionData", max<size_t>(1, mCurFrame->mResources.distribution_data_size)*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);

	memcpy(mCurFrame->mInstances.data(), instanceDatas.data(), instanceDatas.size()*sizeof(InstanceData));
	memcpy(mCurFrame->mMaterialData.data(), materialData.data.data(), materialData.data.size()*sizeof(uint32_t));
	memcpy(mCurFrame->mLightInstances.data(), lightInstances.data(), lightInstances.size()*sizeof(uint32_t));
	for (const auto&[buf, address] : mCurFrame->mResources.distribution_data_map)
		commandBuffer.copy_buffer(buf, Buffer::View<float>(mCurFrame->mDistributionData, address, buf.size()));

	commandBuffer.barrier({ mCurFrame->mDistributionData }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead );
	commandBuffer.hold_resource(mCurFrame->mDistributionData);

	mPathTracePushConstants.gLightCount = (uint32_t)lightInstances.size();
}

void RayTraceScene::update(CommandBuffer& commandBuffer, const float deltaTime) {
	ProfilerRegion ps("RayTraceScene::update", commandBuffer);

	for (const string& file : commandBuffer.mDevice.mInstance.window().input_state().files()) {
		const fs::path filepath = file;
		const string name = filepath.filename().string();
		load_scene(mNode.make_child(name), commandBuffer, filepath);
	}

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

		mCurFrame->mResources = {};
		mCurFrame->mInstanceTransformMap = {};
		mCurFrame->mResources.distribution_data_size = 0;
		mCurFrame->mFence = commandBuffer.fence();
	}

	if (!mCurFrame || mUpdateSceneEachFrame) {
		update_scene(commandBuffer, deltaTime);
	} else {
		mCurFrame->mScene = mPrevFrame->mScene;
		mCurFrame->mInstanceTransformMap = mPrevFrame->mInstanceTransformMap;
		mCurFrame->mVertices = mPrevFrame->mVertices;
		mCurFrame->mIndices = mPrevFrame->mIndices;
		mCurFrame->mMaterialData = mPrevFrame->mMaterialData;
		mCurFrame->mInstances = mPrevFrame->mInstances;
		mCurFrame->mLightInstances = mPrevFrame->mLightInstances;
		mCurFrame->mDistributionData = mPrevFrame->mDistributionData;
		mCurFrame->mInstanceIndexMap = mPrevFrame->mInstanceIndexMap;
	}

	if (!mCurFrame->mPathTraceDescriptorSet) mCurFrame->mPathTraceDescriptorSet = make_shared<DescriptorSet>(mPathTraceDescriptorSetLayouts[0], "path_trace_scene_descriptorset");
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gScene"), **mCurFrame->mScene);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gVertices"), mCurFrame->mVertices);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gIndices"), mCurFrame->mIndices);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gInstances"), mCurFrame->mInstances);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gMaterialData"), mCurFrame->mMaterialData);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gDistributions"), mCurFrame->mDistributionData);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gLightInstances"), mCurFrame->mLightInstances);
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gCounters"), mCounterValues);
	for (const auto&[image, index] : mCurFrame->mResources.images)
		mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gImages"), index, image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead));
	for (const auto&[vol, index] : mCurFrame->mResources.volume_data_map)
		mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gVolumes"), index, vol);
	mCurFrame->mPathTraceDescriptorSet->flush_writes();

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mCounterValues[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mCounterValues[0];
		mRaysPerSecondTimer = 0;
	}
}

void RayTraceScene::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<hlsl::ViewData>& views) {
	if (!mCurFrame) return;

	ProfilerRegion ps("RayTraceScene::render", commandBuffer);

	// Initialize buffers

	const vk::Extent3D extent = renderTarget.extent();
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		ProfilerRegion ps("create images");

		mCurFrame->mFrameNumber = 0;
		mCurFrame->mGradientSamples.reset();

		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);

		mCurFrame->mRadianceMutex = make_shared<Buffer>(commandBuffer.mDevice, "gRadianceMutex", extent.width*extent.height*sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mVisibility = make_shared<Buffer>(commandBuffer.mDevice, "gVisibility", extent.width*extent.height*sizeof(VisibilityInfo), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mPathStates = make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", extent.width*extent.height*sizeof(PathState), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mPathStateVertices = make_shared<Buffer>(commandBuffer.mDevice, "gPathStateVertices", extent.width*extent.height*sizeof(PathVertex), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mPathStateShadingData = make_shared<Buffer>(commandBuffer.mDevice, "gPathStateShadingData", extent.width*extent.height*sizeof(ShadingData), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mReservoirs = make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", extent.width*extent.height*sizeof(Reservoir), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);

		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", extent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);

		for (Image::View& v : mCurFrame->mTemp)
			v = make_shared<Image>(commandBuffer.mDevice, "pingpong", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);

		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
	}
	const size_t max_light_vertices = extent.width*extent.height*max(1u,mPathTracePushConstants.gMaxLightDepth);
	if (!mCurFrame->mLightPathVertices || mCurFrame->mLightPathVertices.size() < max_light_vertices) {
		ProfilerRegion ps("create light path buffers");
		mCurFrame->mLightPathVertices = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathVertices", max_light_vertices*sizeof(PathVertex), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mLightPathShadingData = make_shared<Buffer>(commandBuffer.mDevice, "gLightPathShadingData", max_light_vertices*sizeof(ShadingData), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	}

	const uint32_t& gGradientDownsample = mCreateGradientSamplesPipeline->specialization_constant("gGradientDownsample");
	const vk::Extent3D gradExtent((extent.width + gGradientDownsample-1) / gGradientDownsample, (extent.height + gGradientDownsample-1) / gGradientDownsample, 1);
	if (!mCurFrame->mGradientSamples || mCurFrame->mGradientSamples.extent() != gradExtent) {
		ProfilerRegion ps("create gradient images");
		mCurFrame->mGradientSamples = make_shared<Image>(commandBuffer.mDevice, "gGradientSamples", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		for (array<Image::View, 2>& v : mCurFrame->mDiffTemp) {
			v[0] = make_shared<Image>(commandBuffer.mDevice, "diff1 pingpong", gradExtent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
			v[1] = make_shared<Image>(commandBuffer.mDevice, "diff2 pingpong", gradExtent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		}
	}

	const bool hasHistory = mPrevFrame && mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	// upload views
	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size()*sizeof(hlsl::ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(mCurFrame->mViews.data(), views.data(), mCurFrame->mViews.size_bytes());

	// per-frame push constants
	PathTracePushConstants push_constants = mPathTracePushConstants;
	push_constants.gViewCount = (uint32_t)views.size();
	if (mRandomPerFrame) push_constants.gRandomSeed = rand();

	// check if views are inside a volume
	mCurFrame->mViewVolumeIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewVolumeInstances", views.size()*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mCurFrame->mViewVolumeIndices, INVALID_INSTANCE);
	bool has_volumes = false;
	mNode.for_each_descendant<HeterogeneousVolume>([&](const component_ptr<HeterogeneousVolume>& vol) {
		has_volumes = true;
		for (uint32_t i = 0; i < views.size(); i++) {
			const float3 view_pos = views[i].camera_to_world.transform_point(float3::Zero());
			const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point(view_pos);
			if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
				mCurFrame->mViewVolumeIndices[i] = mCurFrame->mInstanceTransformMap.at(vol.get()).second;
		}
	});

	uint32_t sampling_flags = mRandomWalkPipeline->specialization_constant("gSamplingFlags");
	if (push_constants.gEnvironmentSampleProbability == 0 || push_constants.gEnvironmentMaterialAddress == -1)
		sampling_flags &= ~SAMPLE_FLAG_SAMPLE_ENVIRONMENT;
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


	shared_ptr<DescriptorSet> descriptor_set_1;
	{
		ProfilerRegion ps("Create descriptor_set_1");
		// set descriptors
		descriptor_set_1 = make_shared<DescriptorSet>(mPathTraceDescriptorSetLayouts[1], "path_trace_descriptorset 1");
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPathStates"), mCurFrame->mPathStates);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPathStateVertices"), mCurFrame->mPathStateVertices);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPathStateShadingData"), mCurFrame->mPathStateShadingData);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gLightPathVertices"), mCurFrame->mLightPathVertices);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gLightPathShadingData"), mCurFrame->mLightPathShadingData);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gViews"), mCurFrame->mViews);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevViews"), hasHistory ? mPrevFrame->mViews : mCurFrame->mViews);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gViewVolumeInstances"), mCurFrame->mViewVolumeIndices);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gInstanceIndexMap"), mCurFrame->mInstanceIndexMap);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gRadianceMutex"), mCurFrame->mRadianceMutex);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevRadiance"), image_descriptor(hasHistory ? mPrevFrame->mRadiance : mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevAlbedo"), image_descriptor(hasHistory ? mPrevFrame->mAlbedo : mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gReservoirs"), mCurFrame->mReservoirs);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevReservoirs"), hasHistory ? mPrevFrame->mReservoirs : mCurFrame->mReservoirs);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gAccumColor"), image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevAccumColor"), image_descriptor(hasHistory ? mPrevFrame->mAccumColor : mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gAccumMoments"), image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevAccumMoments"), image_descriptor(hasHistory ? mPrevFrame->mAccumMoments : mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gGradientSamples"), image_descriptor(mCurFrame->mGradientSamples, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gFilterImages"), 0, image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gFilterImages"), 1, image_descriptor(mCurFrame->mTemp[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gDiffImage1"), 0, image_descriptor(mCurFrame->mDiffTemp[0][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gDiffImage2"), 0, image_descriptor(mCurFrame->mDiffTemp[0][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gDiffImage1"), 1, image_descriptor(mCurFrame->mDiffTemp[1][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gDiffImage2"), 1, image_descriptor(mCurFrame->mDiffTemp[1][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite));
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gVisibility"), mCurFrame->mVisibility);
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevVisibility"), hasHistory ? mPrevFrame->mVisibility : mCurFrame->mVisibility);
	}

	auto bind_descriptors_and_push_constants = [&](){
		commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &push_constants);
	};

	commandBuffer.clear_color_image(mCurFrame->mRadiance, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));
	commandBuffer.clear_color_image(mCurFrame->mGradientSamples, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));

	mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

	if (mPathTracePushConstants.gMaxLightDepth > 0) { // Light paths
		mPathTracePushConstants.gNumLightPaths = extent.width * extent.height;
		commandBuffer->fillBuffer(**mCurFrame->mRadianceMutex.buffer(), mCurFrame->mRadianceMutex.offset(), mCurFrame->mRadianceMutex.size_bytes(), 0);
		{
			ProfilerRegion ps("Sample photons", commandBuffer);
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.bind_pipeline(mSamplePhotonsPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
		}
		{
			ProfilerRegion ps("Trace light paths", commandBuffer);
			mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			commandBuffer.barrier({ mCurFrame->mRadianceMutex }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			mRandomWalkPipeline->specialization_constant("gSamplingFlags") = mSamplePhotonsPipeline->specialization_constant("gSamplingFlags");
			commandBuffer.bind_pipeline(mRandomWalkPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			bind_descriptors_and_push_constants();
			commandBuffer.dispatch_over(extent);
			mRandomWalkPipeline->specialization_constant("gSamplingFlags") = sampling_flags;
		}
	}

	{ // Visibility
		ProfilerRegion ps("Sample visibility", commandBuffer);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mPathTraceDescriptorSet->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		commandBuffer.bind_pipeline(mSampleVisibilityPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	if (hasHistory && mReprojection && mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") > 0) {
		// forward project
		ProfilerRegion ps("Forward projection", commandBuffer);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData, mCurFrame->mVisibility }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		mGradientForwardProjectPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		if (mRandomPerFrame) mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = mCurFrame->mFrameNumber;
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(gradExtent);
	}

	if (mPathTracePushConstants.gMaxEyeDepth > 2) { // Eye paths
		ProfilerRegion ps("Trace eye paths", commandBuffer);
		mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		if (sampling_flags & SAMPLE_FLAG_SAMPLE_LIGHT_PATHS)
			commandBuffer.barrier({ mCurFrame->mLightPathVertices, mCurFrame->mLightPathShadingData }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		if (sampling_flags & SAMPLE_FLAG_SAMPLE_RESERVOIRS)
			commandBuffer.barrier({ mCurFrame->mReservoirs }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathStateVertices, mCurFrame->mPathStateShadingData, mCurFrame->mVisibility }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mRandomWalkPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		bind_descriptors_and_push_constants();
		commandBuffer.dispatch_over(extent);
	}

	{
		ProfilerRegion ps("Resolve Samples", commandBuffer);
		mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mCurFrame->mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mResolvePipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		mResolvePipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	Image::View tonemap_in = mCurFrame->mRadiance;
	Image::View tonemap_out = mCurFrame->mTemp[0];

	if (hasHistory && mReprojection) {
		commandBuffer.barrier({ mCurFrame->mVisibility }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

		if (mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") > 0) {
			{ // create diff image
				ProfilerRegion ps("Create diff image", commandBuffer);
				mCurFrame->mRadiance.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->specialization_constant("gModulateAlbedo") = (bool)(sampling_flags & SAMPLE_FLAG_DEMODULATE_ALBEDO);
				mCreateGradientSamplesPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
				commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
				commandBuffer.bind_descriptor_set(1, descriptor_set_1);
				mCreateGradientSamplesPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(gradExtent);
			}
			if (mDiffAtrousIterations > 0) { // filter diff image
				ProfilerRegion ps("Filter diff image", commandBuffer);
				commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
				commandBuffer.bind_descriptor_set(1, descriptor_set_1);
				mAtrousGradientPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
				mAtrousGradientPipeline->push_constants(commandBuffer);
				for (int i = 0; i < mDiffAtrousIterations; i++) {

					mCurFrame->mDiffTemp[0][0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mDiffTemp[1][0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mDiffTemp[0][1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mDiffTemp[1][1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

					commandBuffer.push_constant<uint32_t>("gIteration", i);
					commandBuffer.push_constant<uint32_t>("gStepSize", (1 << i));
					if (i > 0) mAtrousGradientPipeline->transition_images(commandBuffer);
					commandBuffer.dispatch_over(gradExtent);
				}
			}
		}

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);
			mCurFrame->mDiffTemp[0][0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mCurFrame->mDiffTemp[1][0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mCurFrame->mDiffTemp[0][1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mCurFrame->mDiffTemp[1][1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			mTemporalAccumulationPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
			mTemporalAccumulationPipeline->push_constant<uint32_t>("gAtrousGradientIterations") = (uint32_t)mDiffAtrousIterations;

			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
			commandBuffer.bind_descriptor_set(1, descriptor_set_1);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			if (!mTemporalAccumulationPipeline->specialization_constant("gUseVisibility"))
				if ((mCurFrame->mViews[0].camera_to_world.transform_point(float3::Zero()) != mPrevFrame->mViews[0].camera_to_world.transform_point(float3::Zero())).any())
					commandBuffer.push_constant<uint32_t>("gHistoryLimit", 0);
			commandBuffer.dispatch_over(extent);
		}

		tonemap_in = mCurFrame->mAccumColor;

		{ // estimate variance
			ProfilerRegion ps("Estimate Variance", commandBuffer);
			mCurFrame->mAccumColor.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			mCurFrame->mAccumMoments.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

			commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			commandBuffer.bind_descriptor_set(1, descriptor_set_1);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		if (mAtrousIterations > 0) {
			ProfilerRegion ps("Filter image", commandBuffer);
			mAtrousPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
			commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			commandBuffer.bind_descriptor_set(1, descriptor_set_1);
			mAtrousPipeline->push_constants(commandBuffer);

			for (uint32_t i = 0; i < mAtrousIterations; i++) {
				mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCurFrame->mTemp[1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

				commandBuffer.push_constant<uint32_t>("gIteration", i);
				commandBuffer.push_constant<uint32_t>("gStepSize", 1 << i);
				if (i > 0) mAtrousPipeline->transition_images(commandBuffer);
				commandBuffer.dispatch_over(extent);

				if (i+1 == mHistoryTap) {
					mCurFrame->mTemp[0].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
					mCurFrame->mTemp[1].transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);

					commandBuffer.bind_pipeline(mCopyRGBPipeline->get_pipeline());
					commandBuffer.bind_descriptor_set(1, descriptor_set_1);
					mCopyRGBPipeline->transition_images(commandBuffer);
					commandBuffer.dispatch_over(extent);

					if (i+1 < mAtrousIterations) {
						commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
						commandBuffer.bind_descriptor_set(1, descriptor_set_1);
						mAtrousPipeline->push_constants(commandBuffer);
					}
				}
			}
			tonemap_in = mCurFrame->mTemp[mAtrousIterations%2];
			tonemap_out = mCurFrame->mTemp[(mAtrousIterations+1)%2];
		}
	} else
		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });

	{ // Tonemap
		ProfilerRegion ps("Tonemap", commandBuffer);
		mTonemapPipeline->descriptor("gInput") = image_descriptor(tonemap_in, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(tonemap_out, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

		mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gDebug2") = image_descriptor(mCurFrame->mDiffTemp[mDiffAtrousIterations%2][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		if (mTonemapPipeline->specialization_constant("gDebugMode") == DebugMode::eAccumLength)
			commandBuffer.push_constant("gExposure", 1/mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit"));
		else if (mTonemapPipeline->specialization_constant("gDebugMode") == DebugMode::eRelativeTemporalGradient || mTonemapPipeline->specialization_constant("gDebugMode") == DebugMode::eTemporalGradient)
			commandBuffer.push_constant("gExposure", mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"));
		else
			mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	if (tonemap_out.image()->format() == renderTarget.image()->format())
		commandBuffer.copy_image(tonemap_out, renderTarget);
	else
		commandBuffer.blit_image(tonemap_out, renderTarget);
}

}