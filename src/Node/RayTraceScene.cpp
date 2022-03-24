#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#include <stb_image_write.h>

#include <random>

using namespace stm::hlsl;

namespace stm {
	
AccelerationStructure::AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureGeometryKHR>& geometries,  const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges) : DeviceResource(commandBuffer.mDevice, name) {
	vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(type,vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, vk::BuildAccelerationStructureModeKHR::eBuild);
	buildGeometry.setGeometries(geometries);

	vector<uint32_t> counts((uint32_t)geometries.size());
	for (uint32_t i = 0; i < geometries.size(); i++)
		counts[i] = (buildRanges.data() + i)->primitiveCount;
	vk::AccelerationStructureBuildSizesInfoKHR buildSizes = commandBuffer.mDevice->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, counts);

	mBuffer = make_shared<Buffer>(commandBuffer.mDevice, name, buildSizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);
	Buffer::View<byte> scratchBuf = make_shared<Buffer>(commandBuffer.mDevice, name + "/ScratchBuffer", buildSizes.buildScratchSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress|vk::BufferUsageFlagBits::eStorageBuffer);

	mAccelerationStructure = commandBuffer.mDevice->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR({}, **mBuffer.buffer(), mBuffer.offset(), mBuffer.size_bytes(), type));

	buildGeometry.dstAccelerationStructure = mAccelerationStructure;
	buildGeometry.scratchData = scratchBuf.device_address();
	commandBuffer->buildAccelerationStructuresKHR(buildGeometry, buildRanges.data());
	commandBuffer.hold_resource(scratchBuf);
}
AccelerationStructure::~AccelerationStructure() {
	if (mAccelerationStructure)
		mDevice->destroyAccelerationStructureKHR(mAccelerationStructure);
}

namespace hlsl {
#include <HLSL/kernels/a-svgf/svgf_shared.hlsli>
#include <HLSL/tonemap.hlsli>
}

void inspector_gui_fn(RayTraceScene* v) { v->on_inspector_gui(); }

RayTraceScene::RayTraceScene(Node& node) : mNode(node), mCurFrame(make_unique<FrameResources>()), mPrevFrame(make_unique<FrameResources>()) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Gui>()->register_inspector_gui_fn(&inspector_gui_fn);

	create_pipelines();

	mCurFrame->mFrameId = mPrevFrame->mFrameId = 0;
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

	optional<uint32_t> debugMode;
	if (mTraceVisibilityPipeline) {
		debugMode = mTonemapPipeline->specialization_constant("gDebugMode");
		mNode.node_graph().erase_recurse(*mTraceVisibilityPipeline.node().parent());
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
		if (name == "gSampler") b.mImmutableSamplers = { samplerRepeat };
		if (name == "gVolumes" || name == "gImages" || name == "g3DImages") b.mBindingFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
		b.mStageFlags = vk::ShaderStageFlagBits::eCompute;
		bindings[binding.mSet].emplace(binding.mBinding, b);
		mPathTraceDescriptorMap[binding.mSet].emplace(name, binding.mBinding);
	};
	for (const auto[name,binding] : shaders.at("pt_trace_visibility")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_store_visibility")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_store_albedo_and_reservoirs")->descriptors()) make_binding_fn(name, binding);
	for (const auto[name,binding] : shaders.at("pt_sample_path_bounce")->descriptors()) make_binding_fn(name, binding);
	for (uint32_t i = 0; i < 2; i++)
		mPathTraceDescriptorSetLayouts[i] = make_shared<DescriptorSetLayout>(instance->device(), "path_trace_descriptor_set_layout" + to_string(i), bindings[i]);
	
	mTraceVisibilityPipeline = n.make_child("pt_trace_visibility").make_component<ComputePipelineState>("pt_trace_visibility", shaders.at("pt_trace_visibility"));
	mStoreVisibilityPipeline = n.make_child("pt_store_visibility").make_component<ComputePipelineState>("pt_store_visibility", shaders.at("pt_store_visibility"));
	mSampleAlbedoAndReservoirsPipeline = n.make_child("pt_store_albedo_and_reservoirs").make_component<ComputePipelineState>("pt_store_albedo_and_reservoirs", shaders.at("pt_store_albedo_and_reservoirs"));
	mSamplePathBouncePipeline = n.make_child("pt_sample_path_bounce").make_component<ComputePipelineState>("pt_sample_path_bounce", shaders.at("pt_sample_path_bounce"));

	mPathTracePushConstants.gReservoirSamples = 0;
	mPathTracePushConstants.gMaxNullCollisions = 64;
	mPathTracePushConstants.gSamplingFlags = SAMPLE_FLAG_BG_IS | SAMPLE_FLAG_LIGHT_IS | SAMPLE_FLAG_MIS | SAMPLE_FLAG_RAY_CONE_LOD;
	
	mCounterValues = make_shared<Buffer>(instance->device(), "gCounters", sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
	memset(mCounterValues.data(), 0, mCounterValues.size_bytes());
	mRaysPerSecond = 0;
	mRaysPerSecondTimer = 0;

	mDemodulateAlbedoPipeline = n.make_child("tonemap_demodulate_albedo").make_component<ComputePipelineState>("tonemap_demodulate_albedo", shaders.at("tonemap_demodulate_albedo"));
	
	mTonemapPipeline = n.make_child("tonemap").make_component<ComputePipelineState>("tonemap", shaders.at("tonemap"));
	mTonemapPipeline->push_constant<float>("gExposure") = 1;
	if (debugMode) mTonemapPipeline->specialization_constant("gDebugMode") = *debugMode;

	mGradientForwardProjectPipeline = n.make_child("gradient_forward_project").make_component<ComputePipelineState>("gradient_forward_project", shaders.at("gradient_forward_project"));
	mCreateGradientSamplesPipeline = n.make_child("create_gradient_samples").make_component<ComputePipelineState>("create_gradient_samples", shaders.at("create_gradient_samples"));
	mAtrousGradientPipeline = n.make_child("atrous_gradient").make_component<ComputePipelineState>("atrous_gradient", shaders.at("atrous_gradient"));
	mAtrousGradientPipeline->push_constant<float>("gSigmaLuminanceBoost") = 1;
	mTemporalAccumulationPipeline = n.make_child("temporal_accumulation").make_component<ComputePipelineState>("temporal_accumulation", shaders.at("temporal_accumulation"));
	mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit") = 128;
	mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") = 1.f;
	mTemporalAccumulationPipeline->push_constant<uint32_t>("gGradientDownsample") = 3;
	mTemporalAccumulationPipeline->set_immutable_sampler("gSampler", samplerClamp);
	mEstimateVariancePipeline = n.make_child("estimate_variance").make_component<ComputePipelineState>("estimate_variance", shaders.at("estimate_variance"));
	mAtrousPipeline = n.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
	mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost") = 3;
	mCopyRGBPipeline = n.make_child("copy_rgb").make_component<ComputePipelineState>("atrous_copy_rgb", shaders.at("atrous_copy_rgb"));
}

void RayTraceScene::on_inspector_gui() {
	const auto[rps, ext] = format_number(mRaysPerSecond);
	ImGui::Text("%.2f%s Rays/second", rps, ext);

	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(40);
		ImGui::DragScalar("Max Depth", ImGuiDataType_U32, &mMaxDepth, 1);
		ImGui::DragScalar("Min Depth", ImGuiDataType_U32, &mMinDepth, 1);
		ImGui::DragScalar("Direct Light Sampling Depth", ImGuiDataType_U32, &mDirectLightDepth, 1);
		ImGui::DragScalar("Reservoir Samples", ImGuiDataType_U32, &mPathTracePushConstants.gReservoirSamples, 1);
		ImGui::DragScalar("Max Null Collisions", ImGuiDataType_U32, &mPathTracePushConstants.gMaxNullCollisions, 1);

		ImGui::PopItemWidth();

		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		uint32_t& samplingFlags = mPathTracePushConstants.gSamplingFlags;
		ImGui::CheckboxFlags("Importance Sample Lights", &samplingFlags, SAMPLE_FLAG_LIGHT_IS);
		ImGui::CheckboxFlags("Importance Sample Background", &samplingFlags, SAMPLE_FLAG_BG_IS);
		ImGui::CheckboxFlags("Multiple Importance", &samplingFlags, SAMPLE_FLAG_MIS);
		ImGui::CheckboxFlags("Ray Cone LoD", &samplingFlags, SAMPLE_FLAG_RAY_CONE_LOD);
		if (samplingFlags & SAMPLE_FLAG_BG_IS) {
			ImGui::PushItemWidth(40);
			ImGui::DragFloat("Environment Sample Probability", &mPathTracePushConstants.gEnvironmentSampleProbability, .1f, 0, 1);
			ImGui::PopItemWidth();
		}
		ImGui::Checkbox("Demodulate Albedo", &mDemodulateAlbedo);
	}

	if (ImGui::CollapsingHeader("Denoising")) {
		ImGui::Checkbox("Reprojection", &mReprojection);
		if (mReprojection) {
			if (ImGui::Checkbox("Use Visibility", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gUseVisibility")))) {
				mEstimateVariancePipeline->specialization_constant("gUseVisibility") = mTemporalAccumulationPipeline->specialization_constant("gUseVisibility");
				mAtrousPipeline->specialization_constant("gUseVisibility") = mTemporalAccumulationPipeline->specialization_constant("gUseVisibility");
			}
			ImGui::PushItemWidth(40);
			//ImGui::Checkbox("Disable Sample Rejection", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gDisableReprojection")));
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

	uint32_t m = mTonemapPipeline->specialization_constant("gDebugMode");
  if (ImGui::BeginCombo("Debug Mode", to_string((DebugMode)m).c_str())) {
    for (uint32_t i = 0; i < DebugMode::eDebugModeCount; i++)
      if (ImGui::Selectable(to_string((DebugMode)i).c_str(), m == i))
        mTonemapPipeline->specialization_constant("gDebugMode") = i;
    ImGui::EndCombo();
	}

	if (mPrevFrame->mRadiance) {
		static char path[MAX_PATH] { 'i', 'm', 'a', 'g', 'e', '.', 'h', 'd', 'r', '\0' };
		ImGui::InputText("Output HDR", path, MAX_PATH);
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
			cb->completion_fence().wait();

			stbi_write_hdr(path, src.extent().width, src.extent().height, 4, pixels.data());
		}
	}
}

void RayTraceScene::update(CommandBuffer& commandBuffer, float deltaTime) {
	ProfilerRegion s("RayTraceScene::update", commandBuffer);

	swap(mPrevFrame, mCurFrame);
	mCurFrame->mFrameId = mPrevFrame->mFrameId + 1;

	vector<vk::BufferMemoryBarrier> blasBarriers;

	mCurFrame->mResources = {};
	mCurFrame->mResources.distribution_data_size = 0;

	uint32_t totalVertexCount = 0;
	uint32_t totalIndexBufferSize = 0;
	ByteAppendBuffer materialData;
	materialData.data.reserve(mPrevFrame->mMaterialData ? mPrevFrame->mMaterialData.size()/sizeof(uint32_t) : 1);
	unordered_map<Material*, uint32_t> materialMap;
	
	vector<tuple<MeshPrimitive*, MeshAS*, uint32_t>> instanceIndices;
	vector<vk::AccelerationStructureInstanceKHR> instancesAS;
	vector<InstanceData> instanceDatas;
	instanceDatas.reserve(mPrevFrame->mInstances ? mPrevFrame->mInstances.size() : 1);
	unordered_map<void*, pair<hlsl::TransformData, uint32_t>> transformHistory;
	Buffer::View<uint32_t> instanceIndexMap = make_shared<Buffer>(commandBuffer.mDevice, "InstanceIndexMap", sizeof(uint32_t)*max<size_t>(1, mTransformHistory.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(instanceIndexMap, -1);

	vector<uint32_t> lightInstances;
	lightInstances.reserve(1);

	{ // spheres
		ProfilerRegion s("Process spheres", commandBuffer);
		mNode.for_each_descendant<SpherePrimitive>([&](const component_ptr<SpherePrimitive>& prim) {
			// append unique materials to materials list
			auto materialMap_it = materialMap.find(prim->mMaterial.get());
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *prim->mMaterial);
			}
			if (prim->mMaterial->index() == BSDFType::eEmissive)
				lightInstances.emplace_back((uint32_t)instanceDatas.size());

			const float3 mn = -float3::Ones();
			const float3 mx = float3::Ones();
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
			
			TransformData transform = node_to_world(prim.node());
			float r = prim->mRadius;
			#ifdef TRANSFORM_UNIFORM_SCALING
			r *= transform.mScale;
			transform.mScale = prim->mRadius;
			#else
			const float3 scale = float3(
				transform.m.block(0, 0, 3, 1).matrix().norm(),
				transform.m.block(0, 1, 3, 1).matrix().norm(),
				transform.m.block(0, 2, 3, 1).matrix().norm() );
			const Quaternionf rotation(transform.m.block<3,3>(0,0).matrix() * DiagonalMatrix<float,3,3>(1/scale.x(), 1/scale.y(), 1/scale.z()));
			
			r *= transform.m.block<3,3>(0,0).matrix().determinant();
			transform = make_transform(transform.m.col(3).head<3>(), make_quatf(rotation.x(), rotation.y(), rotation.z(), rotation.w()), float3::Constant(prim->mRadius));
			#endif
			
			TransformData prevTransform;
			if (auto it = mTransformHistory.find(prim.get()); it != mTransformHistory.end()) {
				prevTransform = it->second.first;
				instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = transform;

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_SPHERES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);

			transformHistory.emplace(prim.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
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

			// build BLAS
			auto it = mMeshAccelerationStructures.find(prim->mMesh.get());
			if (it == mMeshAccelerationStructures.end()) {
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
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *prim->mMaterial);
			}
			if (prim->mMaterial->index() == BSDFType::eEmissive)
				lightInstances.emplace_back((uint32_t)instanceDatas.size());
			
			const TransformData transform = node_to_world(prim.node());
			TransformData prevTransform;
			if (auto transform_it = mTransformHistory.find(prim.get()); transform_it != mTransformHistory.end()) {
				prevTransform = transform_it->second.first;
				instanceIndexMap[transform_it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = transform;

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_TRIANGLES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));

			const uint32_t triCount = prim->mMesh->indices().size_bytes() / (prim->mMesh->indices().stride()*3);
			
			instanceIndices.emplace_back(prim.get(), &it->second, (uint32_t)instanceDatas.size());
			transformHistory.emplace(prim.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
			instanceDatas.emplace_back(make_instance_triangles(transform, prevTransform, materialMap_it->second, triCount, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride()));
			totalVertexCount += mMeshVertices.at(prim->mMesh.get()).size();
			totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);

			//if (auto mask = prim->mMaterial.node().find_in_descendants<Image::View>(); mask && mask.node().name() == "alpha_mask") {
			//	const uint32_t ind = mCurFrame->mResources.get_index(*mask);
			//	BF_SET(instanceDatas.back().v[1], ind, 0, 12);
			//} else
			//	BF_SET(instanceDatas.back().v[1], ~0u, 0, 12);
		});
	}
	
	{ // volumes
		ProfilerRegion s("Process heterogeneous volumes", commandBuffer);
		mNode.for_each_descendant<HeterogeneousVolume>([&](const component_ptr<HeterogeneousVolume>& vol) {
			auto materialMap_it = materialMap.find(reinterpret_cast<Material*>(vol.get()));
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(reinterpret_cast<Material*>(vol.get()), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, mCurFrame->mResources, *vol);
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
			
			TransformData prevTransform;
			if (auto it = mTransformHistory.find(vol.get()); it != mTransformHistory.end()) {
				prevTransform = it->second.first;
				instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = transform;

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_VOLUME;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);
			
			transformHistory.emplace(vol.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
			instanceDatas.emplace_back( make_instance_volume(transform, prevTransform, materialMap_it->second, mCurFrame->mResources.volume_data_map.at(vol->density_buffer)) );
		});
	}
	
	mTransformHistory = transformHistory;

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
		mTopLevel = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geom, range);
		commandBuffer.barrier({commandBuffer.hold_resource(mTopLevel).buffer()},
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
			mPathTracePushConstants.gEnvironmentMaterialAddress = address;
			mPathTracePushConstants.gEnvironmentSampleProbability = 0.5f;
		} else {
			mPathTracePushConstants.gEnvironmentMaterialAddress = INVALID_MATERIAL;
			mPathTracePushConstants.gEnvironmentSampleProbability = 0;
		}
	}

	if (!mCurFrame->mVertices || mCurFrame->mVertices.size() < totalVertexCount)
		mCurFrame->mVertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(PackedVertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	if (!mCurFrame->mIndices || mCurFrame->mIndices.size() < totalIndexBufferSize)
		mCurFrame->mIndices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);

	{ // copy vertices and indices
		ProfilerRegion s("Copy vertex data", commandBuffer);
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

	commandBuffer.barrier({mCurFrame->mDistributionData}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead );

	commandBuffer.hold_resource(mCurFrame->mDistributionData);
	
	mCurFrame->mPathTraceDescriptorSet = make_shared<DescriptorSet>(mPathTraceDescriptorSetLayouts[0], "path_trace_scene_descriptorset");
	mCurFrame->mPathTraceDescriptorSet->insert_or_assign(mPathTraceDescriptorMap[0].at("gScene"), **mTopLevel);
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

	mPathTracePushConstants.gLightCount = (uint32_t)lightInstances.size();

	mGradientForwardProjectPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mGradientForwardProjectPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	mGradientForwardProjectPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mGradientForwardProjectPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	
	mTemporalAccumulationPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;

	mTonemapPipeline->specialization_constant("gModulateAlbedo") = mDemodulateAlbedo;

	mRaysPerSecondTimer += deltaTime;
	if (mRaysPerSecondTimer > 1) {
		mRaysPerSecond = (mCounterValues[0] - mPrevCounterValue) / mRaysPerSecondTimer;
		mPrevCounterValue = mCounterValues[0];
		mRaysPerSecondTimer = 0;
	}
}

void RayTraceScene::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<hlsl::ViewData>& views) {
	if (!mCurFrame->mPathTraceDescriptorSet) return;
	ProfilerRegion ps("RayTraceScene::render", commandBuffer);

	const vk::Extent3D extent = renderTarget.extent();
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		mCurFrame->mFrameId = 0;
		mCurFrame->mGradientSamples.reset();

		for (Image::View& v : mCurFrame->mVisibility)
			v = make_shared<Image>(commandBuffer.mDevice, "gVisibility", extent, vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mReservoirs = make_shared<Buffer>(commandBuffer.mDevice, "gReservoirs", extent.width*extent.height*sizeof(Reservoir), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mPathStates = make_shared<Buffer>(commandBuffer.mDevice, "gPathStates", extent.width*extent.height*sizeof(PathState), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		mCurFrame->mPathVertices = make_shared<Buffer>(commandBuffer.mDevice, "gPathVertices", 2*extent.width*extent.height*sizeof(PathVertexGeometry), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);

		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", extent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		for (Image::View& v : mCurFrame->mTemp)
			v = make_shared<Image>(commandBuffer.mDevice, "pingpong", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		
		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
	}
	
	const uint32_t& gGradientDownsample = mCreateGradientSamplesPipeline->specialization_constant("gGradientDownsample");
	const vk::Extent3D gradExtent((extent.width + gGradientDownsample-1) / gGradientDownsample, (extent.height + gGradientDownsample-1) / gGradientDownsample, 1);
	if (!mCurFrame->mGradientSamples || mCurFrame->mGradientSamples.extent() != gradExtent) {
		mCurFrame->mGradientSamples = make_shared<Image>(commandBuffer.mDevice, "gGradientSamples", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		for (array<Image::View, 2>& v : mCurFrame->mDiffTemp) {
			v[0] = make_shared<Image>(commandBuffer.mDevice, "diff1 pingpong", gradExtent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
			v[1] = make_shared<Image>(commandBuffer.mDevice, "diff2 pingpong", gradExtent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		}
	}
	
	const bool hasHistory = mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size()*sizeof(hlsl::ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);	
	memcpy(mCurFrame->mViews.data(), views.data(), mCurFrame->mViews.size_bytes());
	
	mPathTracePushConstants.gViewCount = (uint32_t)views.size();
	mPathTracePushConstants.gDebugMode = mTonemapPipeline->specialization_constant("gDebugMode");
	if (mRandomPerFrame) mPathTracePushConstants.gRandomSeed = rand();

	mCurFrame->mViewVolumeIndices = make_shared<Buffer>(commandBuffer.mDevice, "gViewVolumeInstances", views.size()*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);	
	ranges::fill(mCurFrame->mViewVolumeIndices, INVALID_INSTANCE);
	mNode.for_each_descendant<HeterogeneousVolume>([&](const component_ptr<HeterogeneousVolume>& vol) {
		for (uint32_t i = 0; i < views.size(); i++) {
			const float3 view_pos = views[i].camera_to_world.transform_point(float3::Zero());
			const float3 local_view_pos = node_to_world(vol.node()).inverse().transform_point(view_pos);
			if (vol->density_grid->grid<float>()->worldBBox().isInside(nanovdb::Vec3R(local_view_pos[0], local_view_pos[1], local_view_pos[2])))
				mCurFrame->mViewVolumeIndices[i] = mTransformHistory.at(vol.get()).second;
		}
	});
	
	shared_ptr<DescriptorSet> descriptor_set_1 = make_shared<DescriptorSet>(mPathTraceDescriptorSetLayouts[1], "path_trace_descriptorset 1");
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gViews"), mCurFrame->mViews);
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPrevViews"), hasHistory ? mPrevFrame->mViews : mCurFrame->mViews);
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gViewVolumeInstances"), mCurFrame->mViewVolumeIndices);
	for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
		descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gVisibility"), i, image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gRadiance"), image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gAlbedo"), image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite));
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPathStates"), mCurFrame->mPathStates);
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gPathVertices"), mCurFrame->mPathVertices);
	descriptor_set_1->insert_or_assign(mPathTraceDescriptorMap[1].at("gReservoirs"), mCurFrame->mReservoirs);

	{ // Visibility
		ProfilerRegion ps("Visibility", commandBuffer);
		mCurFrame->mPathTraceDescriptorSet->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		commandBuffer.bind_pipeline(mTraceVisibilityPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &mPathTracePushConstants);
		commandBuffer.dispatch_over(extent);

		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathVertices }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		commandBuffer.bind_pipeline(mStoreVisibilityPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &mPathTracePushConstants);
		commandBuffer.dispatch_over(extent);
	}

	{ // Direct light, albedo + reservoirs
		ProfilerRegion ps("Albedo and reservoirs", commandBuffer);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathVertices }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mCurFrame->mPathTraceDescriptorSet->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
		for (uint32_t i = 0; i < BSDFType::eBSDFTypeCount; i++) {
			mSampleAlbedoAndReservoirsPipeline->specialization_constant("gMaterialType") = i;
			commandBuffer.bind_pipeline(mSampleAlbedoAndReservoirsPipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
			commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
			commandBuffer.bind_descriptor_set(1, descriptor_set_1);
			commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &mPathTracePushConstants);
			commandBuffer.dispatch_over(extent);
		}
	}

	commandBuffer.clear_color_image(mCurFrame->mGradientSamples, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));

	if (hasHistory && mReprojection && mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") > 0) {
		// forward project
		ProfilerRegion ps("Forward projection", commandBuffer);
		commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathVertices }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gViews") = mCurFrame->mViews;
		for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++) {
			mGradientForwardProjectPipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mGradientForwardProjectPipeline->descriptor("gPrevVisibility", i) = image_descriptor(mPrevFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		}
		mGradientForwardProjectPipeline->descriptor("gPathStates") = mCurFrame->mPathStates;
		mGradientForwardProjectPipeline->descriptor("gPathVertices") = mCurFrame->mPathVertices;
		mGradientForwardProjectPipeline->descriptor("gReservoirs") = mCurFrame->mReservoirs;
		mGradientForwardProjectPipeline->descriptor("gPrevReservoirs") = mPrevFrame->mReservoirs;
		mGradientForwardProjectPipeline->descriptor("gGradientSamples") = image_descriptor(mCurFrame->mGradientSamples, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite|vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		if (mRandomPerFrame) mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = mCurFrame->mFrameId;
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline());
		mGradientForwardProjectPipeline->bind_descriptor_sets(commandBuffer);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(gradExtent);
	}
	
	{ // Trace paths
		ProfilerRegion ps("Trace paths", commandBuffer);
		commandBuffer.barrier({mCurFrame->mReservoirs}, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	
		commandBuffer.bind_pipeline(mSamplePathBouncePipeline->get_pipeline(mPathTraceDescriptorSetLayouts));
		commandBuffer.bind_descriptor_set(0, mCurFrame->mPathTraceDescriptorSet);
		commandBuffer.bind_descriptor_set(1, descriptor_set_1);
		commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PathTracePushConstants), &mPathTracePushConstants);

 		for (uint32_t i = 0; i < mMaxDepth; i++) {
			commandBuffer.barrier({ mCurFrame->mPathStates, mCurFrame->mPathVertices }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			descriptor_set_1->transition_images(commandBuffer, vk::PipelineStageFlagBits::eComputeShader);
			uint32_t flag = mPathTracePushConstants.gSamplingFlags;
			if (i+1 > mMinDepth) flag |= SAMPLE_FLAG_RR;
			if (i > mDirectLightDepth) flag &= ~(SAMPLE_FLAG_BG_IS | SAMPLE_FLAG_LIGHT_IS);
			commandBuffer->pushConstants(commandBuffer.bound_pipeline()->layout(), vk::ShaderStageFlagBits::eCompute, offsetof(PathTracePushConstants, gSamplingFlags), sizeof(uint32_t), &flag);
			commandBuffer.dispatch_over(extent);
		}
	}

	if (mDemodulateAlbedo) {
		ProfilerRegion ps("Demodulate Albedo", commandBuffer);
		mDemodulateAlbedoPipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mDemodulateAlbedoPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mDemodulateAlbedoPipeline->get_pipeline());
		mDemodulateAlbedoPipeline->bind_descriptor_sets(commandBuffer);
		mDemodulateAlbedoPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	Image::View tonemap_in = mCurFrame->mRadiance;
	Image::View tonemap_out = mCurFrame->mTemp[0];

	if (hasHistory && mReprojection) {
		if (mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") > 0) {
			{ // create diff image
				ProfilerRegion ps("Create diff image", commandBuffer);
				mCreateGradientSamplesPipeline->descriptor("gViews")   = mCurFrame->mViews;
				mCreateGradientSamplesPipeline->descriptor("gOutput1") = image_descriptor(mCurFrame->mDiffTemp[0][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gOutput2") = image_descriptor(mCurFrame->mDiffTemp[0][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gRadiance") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gPrevRadiance") = image_descriptor(mPrevFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gPrevAlbedo") = image_descriptor(mPrevFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gGradientSamples") = image_descriptor(mCurFrame->mGradientSamples, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
						mCreateGradientSamplesPipeline->descriptor("gVisibility",i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
				mCreateGradientSamplesPipeline->specialization_constant("gModulateAlbedo") = mDemodulateAlbedo;

				commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline());
				mCreateGradientSamplesPipeline->bind_descriptor_sets(commandBuffer);
				mCreateGradientSamplesPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(gradExtent);
			}
			if (mDiffAtrousIterations > 0) { // filter diff image
				ProfilerRegion ps("Filter diff image", commandBuffer);
				mAtrousGradientPipeline->descriptor("gViews")   = mCurFrame->mViews;
				mAtrousGradientPipeline->descriptor("gImage1",0) = image_descriptor(mCurFrame->mDiffTemp[0][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage2",0) = image_descriptor(mCurFrame->mDiffTemp[0][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage1",1) = image_descriptor(mCurFrame->mDiffTemp[1][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage2",1) = image_descriptor(mCurFrame->mDiffTemp[1][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline());
				mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
				mAtrousGradientPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
				mAtrousGradientPipeline->push_constants(commandBuffer);
				for (int i = 0; i < mDiffAtrousIterations; i++) {
					commandBuffer.push_constant<uint32_t>("gIteration", i);
					commandBuffer.push_constant<uint32_t>("gStepSize", (1 << i));
					if (i > 0) mAtrousGradientPipeline->transition_images(commandBuffer);
					commandBuffer.dispatch_over(gradExtent);
				}
			}
		}

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);
			mTemporalAccumulationPipeline->descriptor("gViews")   = mCurFrame->mViews;
			mTemporalAccumulationPipeline->descriptor("gAccumColor") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAccumMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

			mTemporalAccumulationPipeline->descriptor("gSamples") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gHistory") = image_descriptor(mPrevFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++) {
				mTemporalAccumulationPipeline->descriptor("gVisibility", i)     = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mTemporalAccumulationPipeline->descriptor("gPrevVisibility", i) = image_descriptor(mPrevFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			}
			mTemporalAccumulationPipeline->descriptor("gPrevMoments") = image_descriptor(mPrevFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gDiff") = image_descriptor(mCurFrame->mDiffTemp[mDiffAtrousIterations%2][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			mTemporalAccumulationPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
				
			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline());
			mTemporalAccumulationPipeline->bind_descriptor_sets(commandBuffer);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			if (!mTemporalAccumulationPipeline->specialization_constant("gUseVisibility"))
				if ((mCurFrame->mViews[0].camera_to_world.transform_point(float3::Zero()) != mPrevFrame->mViews[0].camera_to_world.transform_point(float3::Zero())).any())
					commandBuffer.push_constant<uint32_t>("gHistoryLimit", 0);
			commandBuffer.dispatch_over(extent);	
		}

		tonemap_in = mCurFrame->mAccumColor;

		{ // estimate variance
			ProfilerRegion ps("Estimate Variance", commandBuffer);
			mEstimateVariancePipeline->descriptor("gViews") = mCurFrame->mViews;
			mEstimateVariancePipeline->descriptor("gInput") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
				mEstimateVariancePipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
			mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}

		if (mAtrousIterations > 0) {
			ProfilerRegion ps("Filter image", commandBuffer);
			mAtrousPipeline->descriptor("gViews")  = mCurFrame->mViews;
			for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
				mAtrousPipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mAtrousPipeline->descriptor("gImage", 0) = image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			mAtrousPipeline->descriptor("gImage", 1) = image_descriptor(mCurFrame->mTemp[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
			mAtrousPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
			commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
			mAtrousPipeline->bind_descriptor_sets(commandBuffer);
			mAtrousPipeline->push_constants(commandBuffer);

			for (uint32_t i = 0; i < mAtrousIterations; i++) {
				commandBuffer.push_constant<uint32_t>("gIteration", i);
				commandBuffer.push_constant<uint32_t>("gStepSize", 1 << i);
				if (i > 0) mAtrousPipeline->transition_images(commandBuffer);
				commandBuffer.dispatch_over(extent);

				if (i+1 == mHistoryTap) {
					mCopyRGBPipeline->descriptor("gImage", 0) = image_descriptor(mCurFrame->mTemp[(i+1)%2], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
					mCopyRGBPipeline->descriptor("gImage", 1) = image_descriptor(mCurFrame->mAccumColor   , vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
					commandBuffer.bind_pipeline(mCopyRGBPipeline->get_pipeline());
					mCopyRGBPipeline->bind_descriptor_sets(commandBuffer);
					mCopyRGBPipeline->transition_images(commandBuffer);
					commandBuffer.dispatch_over(extent);

					if (i+1 < mAtrousIterations) {
						commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
						mAtrousPipeline->bind_descriptor_sets(commandBuffer);
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