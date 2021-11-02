#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#include <random>

using namespace stm;
using namespace stm::hlsl;

namespace stm {
namespace hlsl{
#include "../HLSL/rt/a-svgf/svgf_shared.hlsli"
}
}

void inspector_gui_fn(RayTraceScene* v) { v->on_inspector_gui(); }

AccelerationStructure::AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureGeometryKHR>& geometries,  const vk::ArrayProxyNoTemporaries<const vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges) : DeviceResource(commandBuffer.mDevice, name) {
	vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(type,vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, vk::BuildAccelerationStructureModeKHR::eBuild);
	buildGeometry.setGeometries(geometries);

	vector<uint32_t> counts((uint32_t)geometries.size());
	for (uint32_t i = 0; i < geometries.size(); i++)
		counts[i] = (buildRanges.data() + i)->primitiveCount;
	vk::AccelerationStructureBuildSizesInfoKHR buildSizes = commandBuffer.mDevice->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, counts);

	mBuffer = make_shared<Buffer>(commandBuffer.mDevice, name, buildSizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);
	Buffer::View<byte> scratchBuf = make_shared<Buffer>(commandBuffer.mDevice, name + "/ScratchBuffer", buildSizes.buildScratchSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);

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

RayTraceScene::RayTraceScene(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
  app->OnUpdate.listen(mNode, bind(&RayTraceScene::update, this, std::placeholders::_1), EventPriority::eLast - 128);
  app->main_pass()->mPass.PostProcess.listen(mNode, [&,app](CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
    draw(commandBuffer, app->main_camera(), framebuffer->at("colorBuffer"));
  }, EventPriority::eFirst + 8);
	app->back_buffer_usage() |= vk::ImageUsageFlagBits::eStorage;
	app->depth_buffer_usage() |= vk::ImageUsageFlagBits::eSampled;
	
	app.node().find_in_descendants<Gui>()->register_inspector_gui_fn(&inspector_gui_fn);

	create_pipelines();
}

void RayTraceScene::create_pipelines() {
	auto instance = mNode.find_in_ancestor<Instance>();

	auto samplerRepeat = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));

	if (mTracePrimaryRaysPipeline)
		mNode.node_graph().erase_recurse(*mTracePrimaryRaysPipeline.node().parent());
	Node& n = mNode.make_child("pipelines");
	
	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();
	
	mCopyVerticesPipeline = n.make_child("copy_vertices_copy").make_component<ComputePipelineState>("copy_vertices_copy", shaders.at("copy_vertices_copy"));
	
	mTracePrimaryRaysPipeline = n.make_child("pathtrace_primary").make_component<ComputePipelineState>("pathtrace_primary", shaders.at("pathtrace_primary"));
	mTracePrimaryRaysPipeline->set_immutable_sampler("gSampler", samplerRepeat);
	mTracePrimaryRaysPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	mTraceIndirectRaysPipeline = n.make_child("pathtrace_indirect").make_component<ComputePipelineState>("pathtrace_indirect", shaders.at("pathtrace_indirect"));
	mTraceIndirectRaysPipeline->set_immutable_sampler("gSampler", samplerRepeat);
	mTraceIndirectRaysPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	mTonemapPipeline = n.make_child("tonemap").make_component<ComputePipelineState>("tonemap", shaders.at("tonemap"));
	mTonemapPipeline->push_constant<float>("gExposure") = 1.f;
	mTonemapPipeline->push_constant<float>("gGamma") = 2.2f;

	mGradientForwardProjectPipeline = n.make_child("gradient_forward_project").make_component<ComputePipelineState>("gradient_forward_project", shaders.at("gradient_forward_project"));
	
	mCreateGradientSamplesPipeline = n.make_child("create_gradient_samples").make_component<ComputePipelineState>("create_gradient_samples", shaders.at("create_gradient_samples"));
	mAtrousGradientPipeline = n.make_child("atrous_gradient").make_component<ComputePipelineState>("atrous_gradient", shaders.at("atrous_gradient"));
	mTemporalAccumulationPipeline = n.make_child("temporal_accumulation").make_component<ComputePipelineState>("temporal_accumulation", shaders.at("temporal_accumulation"));
	mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") = 1.f;
	mEstimateVariancePipeline = n.make_child("estimate_variance").make_component<ComputePipelineState>("estimate_variance", shaders.at("estimate_variance"));
	mAtrousPipeline = n.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
}

void RayTraceScene::on_inspector_gui() {
	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(32);
		ImGui::InputScalar("Bounces", ImGuiDataType_U32, &mTraceIndirectRaysPipeline->specialization_constant("gMaxBounces"));
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Importance Sample Background", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_BG_IS);
		ImGui::CheckboxFlags("Importance Sample Lights", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_LIGHT_IS);
	}
	if (ImGui::CollapsingHeader("Denoising")) {
		ImGui::Checkbox("Enable Denoising", &mDenoise);
		if (ImGui::Checkbox("Modulate Albedo", reinterpret_cast<bool*>(&mTraceIndirectRaysPipeline->specialization_constant("gDemodulateAlbedo"))))
			mTonemapPipeline->specialization_constant("gModulateAlbedo") = mTraceIndirectRaysPipeline->specialization_constant("gDemodulateAlbedo");
		ImGui::SliderFloat("Temporal Alpha", &mTemporalAccumulationPipeline->push_constant<float>("gTemporalAlpha"), 0, 1);
		if (ImGui::CollapsingHeader("Antilag")) {
			ImGui::Checkbox("Enable Antilag", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gAntilag")));
			ImGui::PushItemWidth(32);
			ImGui::InputScalar("Gradient Filter Radius", ImGuiDataType_U32, &mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius"));
			ImGui::PushItemWidth(32);
			ImGui::InputScalar("Gradient Filter Iterations", ImGuiDataType_U32, &mDiffAtrousIterations);
			ImGui::DragFloat("Antilag Scale", &mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"), .01f, 0);
		}
		ImGui::PushItemWidth(32);
		ImGui::InputScalar("Filter Iterations", ImGuiDataType_U32, &mNumIterations);
		if (mNumIterations > 0) {
			ImGui::PushItemWidth(32);
			ImGui::InputScalar("Filter Type", ImGuiDataType_U32, &mAtrousPipeline->specialization_constant("gFilterKernelType"));
			ImGui::PushItemWidth(32);
			ImGui::InputScalar("History Tap Iteration", ImGuiDataType_U32, &mHistoryTap);
		}
		ImGui::PushItemWidth(32);
		ImGui::DragFloat("History Length Threshold", reinterpret_cast<float*>(&mEstimateVariancePipeline->specialization_constant("gHistoryLengthThreshold")), 1);
	}
	if (ImGui::CollapsingHeader("Post Processing")) {
		ImGui::Checkbox("ACES", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant("gACES")));
		ImGui::PushItemWidth(64);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::PushItemWidth(64);
		ImGui::DragFloat("Gamma", &mTonemapPipeline->push_constant<float>("gGamma"), .1f, 0, 5);
		ImGui::InputScalar("Debug Mode", ImGuiDataType_U32, &mTonemapPipeline->specialization_constant("gDebugMode"));
	}
}

void RayTraceScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RasterScene::update", commandBuffer);

	vector<vk::BufferMemoryBarrier> blasBarriers;

	if (!mUnitCubeAS) {
		Buffer::View<vk::AabbPositionsKHR> aabb = make_shared<Buffer>(commandBuffer.mDevice, "Unit cube aabb", sizeof(vk::AabbPositionsKHR), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU);
		aabb[0].minX = -1;
		aabb[0].minY = -1;
		aabb[0].minZ = -1;
		aabb[0].maxX = 1;
		aabb[0].maxY = 1;
		aabb[0].maxZ = 1;
		vk::AccelerationStructureGeometryAabbsDataKHR aabbs(commandBuffer.hold_resource(aabb).device_address(), sizeof(vk::AabbPositionsKHR));
		vk::AccelerationStructureGeometryKHR aabbGeometry(vk::GeometryTypeKHR::eAabbs, aabbs, vk::GeometryFlagBitsKHR::eOpaque);
		vk::AccelerationStructureBuildRangeInfoKHR range(1);
		mUnitCubeAS = make_shared<AccelerationStructure>(commandBuffer, "Unit cube BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, aabbGeometry, range);
		blasBarriers.emplace_back(
			vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
			**mUnitCubeAS->buffer().buffer(), mUnitCubeAS->buffer().offset(), mUnitCubeAS->buffer().size_bytes());
	}

	uint32_t totalVertexCount = 0;
	uint32_t totalIndexBufferSize = 0;
	vector<MaterialData> materials;
	unordered_map<MaterialInfo*, uint32_t> materialMap;
	unordered_map<Image::View, uint32_t> images;
	vector<pair<MeshAS*, uint32_t>> instanceBLAS;
	vector<vk::AccelerationStructureInstanceKHR> instances;
	vector<InstanceData> instanceDatas;
	unordered_map<void*, pair<hlsl::TransformData, uint32_t>> transformHistory;
	Buffer::View<uint32_t> instanceIndexMap = make_shared<Buffer>(commandBuffer.mDevice, "InstanceIndexMap", sizeof(uint32_t)*max<size_t>(1, mTransformHistory.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memset(instanceIndexMap.data(), -1, instanceIndexMap.size_bytes());

	buffer_vector<LightData,16> lights(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	lights.reserve(1);
	mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
		light->mLightToWorld = node_to_world(light.node());
		if (light->mType == LIGHT_TYPE_POINT || light->mType == LIGHT_TYPE_SPOT) {
			PackedLightData p { light->mPackedData };
			p.instance_index((uint32_t)instanceDatas.size());
			
			TransformData prevTransform;
			if (auto it = mTransformHistory.find(light.get()); it != mTransformHistory.end()) {
				prevTransform = it->second.first;
				instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = light->mLightToWorld;
				
			vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
			Eigen::Map<Eigen::Matrix<float, 3, 4, Eigen::RowMajor>>(&instance.transform.matrix[0][0]) = (Eigen::Translation3f(light->mLightToWorld.mTranslation[0], light->mLightToWorld.mTranslation[1], light->mLightToWorld.mTranslation[2]) *
				Eigen::Quaternionf(light->mLightToWorld.mRotation.w, light->mLightToWorld.mRotation.xyz[0], light->mLightToWorld.mRotation.xyz[1], light->mLightToWorld.mRotation.xyz[2]) *
				Eigen::Scaling(p.radius()*light->mLightToWorld.mScale)).matrix().topRows(3);
			instance.instanceCustomIndex = (uint32_t)instances.size();
			instance.mask = 0x2;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**mUnitCubeAS);

			transformHistory.emplace(light.get(), make_pair(light->mLightToWorld, (uint32_t)instanceDatas.size()));
			instanceDatas.emplace_back(light->mLightToWorld, prevTransform, -1, -1, -1, (uint32_t)lights.size() << 8);
			light->mPackedData = p.v;
		}
		lights.emplace_back(*light);
	});

	auto find_image_index = [&](Image::View image) -> uint32_t {
		if (!image) return ~0u;
		auto it = images.find(image);
		return (it == images.end()) ? images.emplace(image, (uint32_t)images.size()).first->second : it->second;
	};

	{ // find env map
		auto envMap = mNode.find_in_descendants<EnvironmentMap>();
		if (envMap) {
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = image_descriptor(envMap->mConditionalDistribution, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = image_descriptor(envMap->mMarginalDistribution, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = find_image_index(envMap->mImage);
		} else {
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = -1;
			Image::View blank = make_shared<Image>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2,1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = image_descriptor(blank, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = image_descriptor(blank, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		}
	}

	// find instances
	mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
		if (prim->mMesh->topology() != vk::PrimitiveTopology::eTriangleList) return;

		// build BLAS
		size_t key = hash_args(prim->mMesh.get(), prim->mFirstIndex, prim->mIndexCount);
		auto it = mMeshAccelerationStructures.find(key);
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
			triangles.indexData = commandBuffer.hold_resource(prim->mMesh->indices()).device_address() + prim->mFirstIndex*prim->mMesh->indices().stride();
			vk::AccelerationStructureGeometryKHR triangleGeometry(vk::GeometryTypeKHR::eTriangles, triangles, vk::GeometryFlagBitsKHR::eOpaque);
			vk::AccelerationStructureBuildRangeInfoKHR range(prim->mIndexCount/3);
			auto as = make_shared<AccelerationStructure>(commandBuffer, prim.node().name()+"/BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, triangleGeometry, range);
			blasBarriers.emplace_back(
				vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());

			it = mMeshAccelerationStructures.emplace(key, MeshAS { as, triangles.maxVertex, 
				prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0],
				prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eNormal)[0],
				prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eTangent)[0],
				prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eTexcoord)[0],
				Buffer::StrideView(prim->mMesh->indices().buffer(), prim->mMesh->indices().stride(), prim->mMesh->indices().offset() + prim->mFirstIndex*prim->mMesh->indices().stride()) }).first;
		}
		
		// append unique materials to materials list
		auto materialMap_it = materialMap.find(prim->mMaterial.get());
		if (materialMap_it == materialMap.end()) {
			materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)materials.size()).first;
			ImageIndices inds { uint4::Zero() };
			inds.albedo(find_image_index(prim->mMaterial->mAlbedoImage));
			inds.normal(find_image_index(prim->mMaterial->mNormalImage));
			inds.emission(find_image_index(prim->mMaterial->mEmissionImage));
			inds.metallic(find_image_index(prim->mMaterial->mMetallicImage));
			inds.roughness(find_image_index(prim->mMaterial->mRoughnessImage));
			inds.occlusion(find_image_index(prim->mMaterial->mOcclusionImage));
			inds.metallic_channel(prim->mMaterial->mMetallicImageComponent);
			inds.roughness_channel(prim->mMaterial->mRoughnessImageComponent);
			inds.occlusion_channel(prim->mMaterial->mOcclusionImageComponent);
			MaterialData& m = materials.emplace_back();
			m.mAlbedo = prim->mMaterial->mAlbedo;
			m.mRoughness = prim->mMaterial->mRoughness;
			m.mEmission = prim->mMaterial->mEmission;
			m.mMetallic = prim->mMaterial->mMetallic;
			m.mAbsorption = prim->mMaterial->mAbsorption;
			m.mNormalScale = prim->mMaterial->mNormalScale;
			m.mOcclusionScale = prim->mMaterial->mOcclusionScale;
			m.mIndexOfRefraction = prim->mMaterial->mIndexOfRefraction;
			m.mTransmission = prim->mMaterial->mTransmission;
			m.mImageIndices = inds.v;
		}
			
		TransformData transform = node_to_world(prim.node());
		TransformData prevTransform;
		if (auto it = mTransformHistory.find(prim.get()); it != mTransformHistory.end()) {
			prevTransform = it->second.first;
			instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
		} else
			prevTransform = transform;

		uint32_t lightIndex = 0;
		if (prim->mMaterial->mEmission.any()) {
			LightData light;
			light.mLightToWorld = transform;
			light.mType = LIGHT_TYPE_EMISSIVE_MATERIAL;
			light.mEmission = prim->mMaterial->mEmission;
			PackedLightData p { float4::Zero() };
			p.instance_index((uint32_t)instanceDatas.size());
			p.prim_count(prim->mIndexCount/3);
			light.mPackedData = p.v;
			lightIndex = (uint32_t)lights.size();
			lights.emplace_back(light);
		}

		vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
		Eigen::Map<Eigen::Matrix<float, 3, 4, Eigen::RowMajor>>(&instance.transform.matrix[0][0]) = (Eigen::Translation3f(transform.mTranslation[0], transform.mTranslation[1], transform.mTranslation[2]) *
			Eigen::Quaternionf(transform.mRotation.w, transform.mRotation.xyz[0], transform.mRotation.xyz[1], transform.mRotation.xyz[2]) *
			Eigen::Scaling(transform.mScale)).matrix().topRows(3);
		instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
		instance.mask = 0x1;
		instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));

		instanceBLAS.emplace_back(&it->second, (uint32_t)instanceDatas.size());
		transformHistory.emplace(prim.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
		instanceDatas.emplace_back(transform, prevTransform, materialMap_it->second, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride() | (lightIndex << 8));
		totalVertexCount += it->second.mVertexCount;
		totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);
	});
	mTransformHistory = transformHistory;
	
	{ // Build TLAS
		commandBuffer.barrier(blasBarriers, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);
		vk::AccelerationStructureGeometryKHR geom { vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryInstancesDataKHR() };
		vk::AccelerationStructureBuildRangeInfoKHR range { (uint32_t)instances.size() };
		if (!instances.empty()) {
			auto buf = make_shared<Buffer>(commandBuffer.mDevice, "TLAS instance buffer", sizeof(vk::AccelerationStructureInstanceKHR)*instances.size(), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
			memcpy(buf->data(), instances.data(), buf->size());
			commandBuffer.hold_resource(buf);
			geom.geometry.instances.data = buf->device_address();
		}
		mTopLevel = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geom, range);
		commandBuffer.barrier(commandBuffer.hold_resource(mTopLevel).buffer(),
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);
	}

	Buffer::View<VertexData> vertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(VertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	Buffer::View<byte> indices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);
	{ // copy vertices and indices
		commandBuffer.hold_resource(vertices);
		commandBuffer.hold_resource(indices);
		mCopyVerticesPipeline->descriptor("gVertices") = vertices;
		for (uint32_t i = 0; i < instanceBLAS.size(); i++) {
			MeshAS& b = *instanceBLAS[i].first;
			InstanceData& d = instanceDatas[instanceBLAS[i].second];

			// copy vertex data
			mCopyVerticesPipeline->descriptor("gPositions") = Buffer::View(b.mPositions.second, b.mPositions.first.mOffset);
			mCopyVerticesPipeline->descriptor("gNormals") = Buffer::View(b.mNormals.second, b.mNormals.first.mOffset);
			mCopyVerticesPipeline->descriptor("gTangents") = Buffer::View(b.mTangents.second, b.mTangents.first.mOffset);
			mCopyVerticesPipeline->descriptor("gTexcoords") = Buffer::View(b.mTexcoords.second, b.mTexcoords.first.mOffset);
			mCopyVerticesPipeline->push_constant<uint32_t>("gCount") = b.mVertexCount;
			mCopyVerticesPipeline->push_constant<uint32_t>("gDstOffset") = d.mFirstVertex;
			mCopyVerticesPipeline->push_constant<uint32_t>("gPositionStride") = b.mPositions.first.mStride;
			mCopyVerticesPipeline->push_constant<uint32_t>("gNormalStride") = b.mNormals.first.mStride;
			mCopyVerticesPipeline->push_constant<uint32_t>("gTangentStride") = b.mTangents.first.mStride;
			mCopyVerticesPipeline->push_constant<uint32_t>("gTexcoordStride") = b.mTexcoords.first.mStride;
			commandBuffer.bind_pipeline(mCopyVerticesPipeline->get_pipeline());
			mCopyVerticesPipeline->bind_descriptor_sets(commandBuffer);
			mCopyVerticesPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(b.mVertexCount);

			// copy indices
			commandBuffer.copy_buffer(b.mIndices, Buffer::View<byte>(indices.buffer(), d.mIndexByteOffset, b.mIndices.size_bytes()));
				
			commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
			commandBuffer.barrier(indices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		}
	}
	
	Buffer::View<InstanceData> instanceBuf = make_shared<Buffer>(commandBuffer.mDevice, "InstanceDatas", sizeof(InstanceData)*max<size_t>(1, instanceDatas.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	Buffer::View<MaterialData> materialBuf = make_shared<Buffer>(commandBuffer.mDevice, "MaterialInfos", sizeof(MaterialData)*max<size_t>(1, materials.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	memcpy(commandBuffer.hold_resource(instanceBuf).data(), instanceDatas.data(), sizeof(InstanceData)*instanceDatas.size());
	memcpy(commandBuffer.hold_resource(materialBuf).data(), materials.data(), sizeof(MaterialData)*materials.size());

	mTracePrimaryRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTracePrimaryRaysPipeline->descriptor("gInstances") = instanceBuf;
	mTracePrimaryRaysPipeline->descriptor("gMaterials") = materialBuf;
	mTracePrimaryRaysPipeline->descriptor("gVertices") = vertices;
	mTracePrimaryRaysPipeline->descriptor("gIndices") = indices;

	mGradientForwardProjectPipeline->descriptor("gInstances") = instanceBuf;
	mGradientForwardProjectPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	mGradientForwardProjectPipeline->descriptor("gVertices") = vertices;
	mGradientForwardProjectPipeline->descriptor("gIndices") = indices;
	
	mTemporalAccumulationPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	
	mTraceIndirectRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTraceIndirectRaysPipeline->descriptor("gInstances") = instanceBuf;
	mTraceIndirectRaysPipeline->descriptor("gMaterials") = materialBuf;
	mTraceIndirectRaysPipeline->descriptor("gVertices") = vertices;
	mTraceIndirectRaysPipeline->descriptor("gIndices") = indices;
	
	mTraceIndirectRaysPipeline->descriptor("gLights") = lights.buffer_view();
	mTraceIndirectRaysPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();

	for (const auto&[image, index] : images) {
		mTraceIndirectRaysPipeline->descriptor("gImages", index) = image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mTracePrimaryRaysPipeline->descriptor("gImages", index) = image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
	}
}

void RayTraceScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer) {
	ProfilerRegion ps("RayTraceScene::draw", commandBuffer);

	vk::Extent3D gradExtent(colorBuffer.extent().width / gGradientDownsample, colorBuffer.extent().height / gGradientDownsample, 1);
	if (!mCurFrame || mCurFrame->mRadiance.extent() != colorBuffer.extent()) {
		mCurFrame = make_unique<FrameData>();
		mCurFrame->mVisibility = make_shared<Image>(commandBuffer.mDevice, "gVisibility", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mRNGSeed = make_shared<Image>(commandBuffer.mDevice, "gRNGSeed", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		
		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance" , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mNormal   = make_shared<Image>(commandBuffer.mDevice, "gNormal", colorBuffer.extent(), vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mZ        = make_shared<Image>(commandBuffer.mDevice, "gZ"       , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mAccumLength  = make_shared<Image>(commandBuffer.mDevice, "gAccumLength", colorBuffer.extent(), vk::Format::eR32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	}
	if (!mPrevUV || mPrevUV.extent() != colorBuffer.extent()) {
		mPrevUV = make_shared<Image>(commandBuffer.mDevice, "gPrevUV", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mGradientPositions = make_shared<Image>(commandBuffer.mDevice, "gGradientPositions", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		mColorHistory = make_shared<Image>(commandBuffer.mDevice, "color history", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mColorHistoryUnfiltered = make_shared<Image>(commandBuffer.mDevice, "unfiltered color history", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mAntilagAlpha = make_shared<Image>(commandBuffer.mDevice, "gAntilagAlpha", colorBuffer.extent(), vk::Format::eR32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc);
		mPing = make_shared<Image>(commandBuffer.mDevice, "pong", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mPong = make_shared<Image>(commandBuffer.mDevice, "ping", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mDiffPing[0] = make_shared<Image>(commandBuffer.mDevice, "diff ping 0", gradExtent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mDiffPing[1] = make_shared<Image>(commandBuffer.mDevice, "diff ping 1", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mDiffPong[0] = make_shared<Image>(commandBuffer.mDevice, "diff pong 0", gradExtent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mDiffPong[1] = make_shared<Image>(commandBuffer.mDevice, "diff pong 1", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
	}

	bool hasHistory = mPrevFrame && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	mCurFrame->mCameraToWorld = node_to_world(camera.node());
	mCurFrame->mProjection = camera->projection((float)colorBuffer.extent().height/(float)colorBuffer.extent().width);

	#pragma pack(push)
	#pragma pack(1)
	struct CameraData {
		TransformData gCameraToWorld;
		ProjectionData gProjection;
		TransformData gWorldToPrevCamera;
		ProjectionData gPrevProjection;
	};
	#pragma pack(pop)
	Buffer::View<CameraData> cameraData = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	cameraData.data()->gCameraToWorld = mCurFrame->mCameraToWorld;
	cameraData.data()->gProjection = mCurFrame->mProjection;
	cameraData.data()->gWorldToPrevCamera = hasHistory ? inverse(mPrevFrame->mCameraToWorld) : inverse(mCurFrame->mCameraToWorld);
	cameraData.data()->gPrevProjection = hasHistory ? mPrevFrame->mProjection : mCurFrame->mProjection;

	{ // Primary rays
		ProfilerRegion ps("Primary rays", commandBuffer);
		mTracePrimaryRaysPipeline->descriptor("gCameraData") = cameraData;
		mTracePrimaryRaysPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gPrevUV") = image_descriptor(mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mTracePrimaryRaysPipeline->get_pipeline());
		mTracePrimaryRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTracePrimaryRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame->mVisibility.extent());
	}

	commandBuffer.clear_color_image(mCurFrame->mRNGSeed, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));
	commandBuffer.clear_color_image(mGradientPositions, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));

	if (mTemporalAccumulationPipeline->specialization_constant("gAntilag") && hasHistory) {
		// forward project
		ProfilerRegion ps("Forward projection", commandBuffer);
		mGradientForwardProjectPipeline->push_constant<TransformData>("gWorldToCamera") = inverse(mCurFrame->mCameraToWorld);
		mGradientForwardProjectPipeline->push_constant<ProjectionData>("gProjection") = mCurFrame->mProjection;
		if (mRandomPerFrame) mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		mGradientForwardProjectPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gRNGSeed") = image_descriptor(mCurFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gGradientSamples") = image_descriptor(mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite|vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevUV") = image_descriptor(mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gPrevVisibility") = image_descriptor(mPrevFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevNormal") = image_descriptor(mPrevFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevZ") = image_descriptor(mPrevFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevRNGSeed") = image_descriptor(mPrevFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline());
		mGradientForwardProjectPipeline->bind_descriptor_sets(commandBuffer);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(gradExtent);
	}
	
	{ // Indirect/shading rays
		ProfilerRegion ps("Indirect rays", commandBuffer);
		mTraceIndirectRaysPipeline->descriptor("gCameraData") = cameraData;
		mTraceIndirectRaysPipeline->descriptor("gVisibility")    = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTraceIndirectRaysPipeline->descriptor("gRNGSeed")       = image_descriptor(mCurFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mTraceIndirectRaysPipeline->descriptor("gRadiance")      = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTraceIndirectRaysPipeline->descriptor("gAlbedo")        = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		if (mRandomPerFrame) mTraceIndirectRaysPipeline->push_constant<uint32_t>("gRandomSeed") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		commandBuffer.bind_pipeline(mTraceIndirectRaysPipeline->get_pipeline());
		mTraceIndirectRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTraceIndirectRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame->mRadiance.extent());
	}

	if (mDenoise && hasHistory) {
		if (mTemporalAccumulationPipeline->specialization_constant("gAntilag")) {
			{
				// create diff image
				ProfilerRegion ps("Create diff image", commandBuffer);
				mCreateGradientSamplesPipeline->descriptor("gOutput1") = image_descriptor(mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gOutput2") = image_descriptor(mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gRadiance") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gPrevRadiance") = image_descriptor(mPrevFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gGradientPositions") = image_descriptor(mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

				commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline());
				mCreateGradientSamplesPipeline->bind_descriptor_sets(commandBuffer);
				mCreateGradientSamplesPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(gradExtent);
			}
			{ // filter diff image
				ProfilerRegion ps("Filter diff image", commandBuffer);
				commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline());
				for (int i = 0; i < mDiffAtrousIterations; i++) {
					mAtrousGradientPipeline->descriptor("gOutput1") = image_descriptor(mDiffPong[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
					mAtrousGradientPipeline->descriptor("gOutput2") = image_descriptor(mDiffPong[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
					mAtrousGradientPipeline->descriptor("gInput1") = image_descriptor(mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
					mAtrousGradientPipeline->descriptor("gInput2") = image_descriptor(mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

					mAtrousGradientPipeline->push_constant<uint32_t>("gIteration") = i;
					mAtrousGradientPipeline->push_constant<uint32_t>("gStepSize") = (1 << i);

					mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
					mAtrousGradientPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(mCurFrame->mVisibility.extent().width/gGradientDownsample, mCurFrame->mVisibility.extent().height/gGradientDownsample);

					std::swap(mDiffPing, mDiffPong);
				}
			}
		}

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);
			mTemporalAccumulationPipeline->descriptor("gAccumColor") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAccumMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAccumLength") = image_descriptor(mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

			mTemporalAccumulationPipeline->descriptor("gColor") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevColor") = image_descriptor(mColorHistory, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevVisibility") = image_descriptor(mPrevFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevMoments") = image_descriptor(mPrevFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gHistoryLength") = image_descriptor(mPrevFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevZ") = image_descriptor(mPrevFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevNormal") = image_descriptor(mPrevFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gDiff") = image_descriptor(mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevUV") = image_descriptor(mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gAntilagAlpha") = image_descriptor(mAntilagAlpha, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline());
			mTemporalAccumulationPipeline->bind_descriptor_sets(commandBuffer);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mCurFrame->mAccumColor.extent());
		}

		commandBuffer.copy_image(mCurFrame->mRadiance, mColorHistoryUnfiltered);
		{ // estimate variance
			ProfilerRegion ps("Estimate Variance", commandBuffer);
			mEstimateVariancePipeline->descriptor("gOutput") = image_descriptor(mPing, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mEstimateVariancePipeline->descriptor("gInput") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gHistoryLength") = image_descriptor(mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
			mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
			mEstimateVariancePipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mPing.extent());
		}

		// atrous
		if (mNumIterations == 0 || mHistoryTap >= mNumIterations)
				commandBuffer.copy_image(mPing, mColorHistory);
		else {
			ProfilerRegion ps("Filter image", commandBuffer);
			mAtrousPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mAtrousPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
			for (uint32_t i = 0; i < mNumIterations; i++) {
				mAtrousPipeline->descriptor("gOutput") = image_descriptor(mPong, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mAtrousPipeline->descriptor("gInput") = image_descriptor(mPing, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mAtrousPipeline->push_constant<uint32_t>("gIteration") = i;
				mAtrousPipeline->push_constant<uint32_t>("gStepSize") = 1 << i;

				mAtrousPipeline->bind_descriptor_sets(commandBuffer);
				mAtrousPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(mPing.extent());

				if (i == mHistoryTap)
					commandBuffer.copy_image(mPong, mColorHistory);

				std::swap(mPing, mPong);
			}
		}

		commandBuffer.blit_image(mPing, colorBuffer);
	} else
		commandBuffer.blit_image(mCurFrame->mRadiance, colorBuffer);

	{ // Tonemap
		ProfilerRegion ps("Tonemap", commandBuffer);
		mTonemapPipeline->descriptor("gColor") = image_descriptor(colorBuffer, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->specialization_constant("gGradientFilterRadius") = mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius");
		uint32_t debugMode = mTonemapPipeline->specialization_constant("gDebugMode");
		mTonemapPipeline->descriptor("gGradientPositions") = image_descriptor(mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gDiff") = image_descriptor(mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gDebug2") = image_descriptor(debugMode == 3 ? mAntilagAlpha : mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(colorBuffer.extent());
	}

	swap(mPrevFrame, mCurFrame);
}