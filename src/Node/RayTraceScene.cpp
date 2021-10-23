#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

using namespace stm;
using namespace stm::hlsl;

namespace stm {
namespace hlsl{
#include "../HLSL/rt/a-svgf/svgf_shared.hlsli"
}
}

void inspector_gui_fn(RayTraceScene* v) { v->on_inspector_gui(); }

AccelerationStructure::AccelerationStructure(CommandBuffer& commandBuffer, const string& name, vk::AccelerationStructureTypeKHR type, const vk::AccelerationStructureGeometryKHR& geometry,  const vk::AccelerationStructureBuildRangeInfoKHR& buildRange) : DeviceResource(commandBuffer.mDevice, name) {
	vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(type,vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, vk::BuildAccelerationStructureModeKHR::eBuild);
	buildGeometry.setGeometries(geometry);

	vk::AccelerationStructureBuildSizesInfoKHR buildSizes = commandBuffer.mDevice->getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, buildRange.primitiveCount);

	mBuffer = make_shared<Buffer>(commandBuffer.mDevice, name, buildSizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);
	Buffer::View<byte> scratchBuf = make_shared<Buffer>(commandBuffer.mDevice, name + "/ScratchBuffer", buildSizes.buildScratchSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress);

	mAccelerationStructure = commandBuffer.mDevice->createAccelerationStructureKHR(vk::AccelerationStructureCreateInfoKHR({}, **mBuffer.buffer(), mBuffer.offset(), mBuffer.size_bytes(), type));

	buildGeometry.dstAccelerationStructure = mAccelerationStructure;
	buildGeometry.scratchData = scratchBuf.device_address();
	commandBuffer->buildAccelerationStructuresKHR(buildGeometry, &buildRange);
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
	auto samplerClamp = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
		0, false, 0, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	if (mTracePrimaryRaysPipeline)
		mNode.node_graph().erase_recurse(*mTracePrimaryRaysPipeline.node().parent());
	Node& n = mNode.make_child("pipelines");
	
	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();
	
	mCopyVerticesPipeline = n.make_child("copy_vertices_copy").make_component<ComputePipelineState>("copy_vertices_copy", shaders.at("copy_vertices_copy"));
	
	mTracePrimaryRaysPipeline = n.make_child("pathtrace_primary").make_component<ComputePipelineState>("pathtrace_primary", shaders.at("pathtrace_primary"));
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
	mTemporalAccumulationPipeline->set_immutable_sampler("gSampler", samplerClamp);
	mEstimateVariancePipeline = n.make_child("estimate_variance").make_component<ComputePipelineState>("estimate_variance", shaders.at("estimate_variance"));
	mAtrousPipeline = n.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
}

void RayTraceScene::on_inspector_gui() {
	ImGui::InputScalar("Bounces", ImGuiDataType_U32, &mTraceIndirectRaysPipeline->specialization_constant("gMaxBounces"));
	ImGui::CheckboxFlags("Importance Sampling", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_IS);
	if (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") & SAMPLE_FLAG_IS)
		ImGui::CheckboxFlags("Background Importance Sampling", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_BG_IS);
	ImGui::InputScalar("Debug Mode", ImGuiDataType_U32, &mTonemapPipeline->specialization_constant("gDebugMode"));
	ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
	ImGui::DragFloat("Gamma", &mTonemapPipeline->push_constant<float>("gGamma"), .1f, 0, 5);
	ImGui::Checkbox("A-SVGF", &mDenoise);
	if (mDenoise) {
		if (ImGui::Checkbox("Modulate Albedo", reinterpret_cast<bool*>(&mTraceIndirectRaysPipeline->specialization_constant("gDemodulateAlbedo"))))
			mTonemapPipeline->specialization_constant("gModulateAlbedo") = mTraceIndirectRaysPipeline->specialization_constant("gDemodulateAlbedo");
		ImGui::SliderFloat("Temporal Alpha", &mTemporalAccumulationPipeline->push_constant<float>("gTemporalAlpha"), 0, 1);
		ImGui::Checkbox("Antilag", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gAntilag")));
		if (mTemporalAccumulationPipeline->specialization_constant("gAntilag")) {
			ImGui::InputScalar("Diff Filter Iterations", ImGuiDataType_U32, &mDiffAtrousIterations);
			ImGui::InputScalar("Gradient Filter Radius", ImGuiDataType_U32, &mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius"));
		}
		ImGui::InputScalar("Filter Iterations", ImGuiDataType_U32, &mNumIterations);
		ImGui::InputScalar("Filter Type", ImGuiDataType_U32, &mAtrousPipeline->specialization_constant("gFilterKernelType"));
	}
}

void RayTraceScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RasterScene::update", commandBuffer);

	//buffer_vector<LightData,16> lights(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	//lights.reserve(1);
	//mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
	//	light->mLightToWorld = node_to_world(light.node());
	//	lights.emplace_back(*light);
	//});

	uint32_t totalVertexCount = 0;
	uint32_t totalIndexBufferSize = 0;
	vector<MaterialData> materials;
	vector<vk::BufferMemoryBarrier> blasBarriers;
	vector<BLAS*> instanceBLAS;
	vector<vk::AccelerationStructureInstanceKHR> instances;
	vector<InstanceData> instanceDatas;
	unordered_map<MaterialInfo*, uint32_t> materialMap;
	unordered_map<Image::View, uint32_t> images;

	auto find_image_index = [&](Image::View image) -> uint32_t {
		if (!image) return ~0u;
		auto it = images.find(image);
		return (it == images.end()) ? images.emplace(image, (uint32_t)images.size()).first->second : it->second;
	};

	{ // find env map
		auto envMap = mNode.find_in_descendants<EnvironmentMap>();
		if (envMap) {
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = sampled_image_descriptor(envMap->mConditionalDistribution);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = sampled_image_descriptor(envMap->mMarginalDistribution);
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = find_image_index(envMap->mImage);
		} else {
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = -1;
			Image::View blank = make_shared<Image>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2,1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = sampled_image_descriptor(blank);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = sampled_image_descriptor(blank);
		}
	}

	// find instances
	mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
		if (prim->mMesh->topology() != vk::PrimitiveTopology::eTriangleList) return;

		// build BLAS
		size_t key = hash_args(prim->mMesh.get(), prim->mFirstIndex, prim->mIndexCount);
		auto it = mAccelerationStructures.find(key);
		if (it == mAccelerationStructures.end()) {
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
			auto as = make_shared<AccelerationStructure>(commandBuffer, prim.node().name()+"/BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel,
				vk::AccelerationStructureGeometryKHR(vk::GeometryTypeKHR::eTriangles, triangles, vk::GeometryFlagBitsKHR::eOpaque),
				vk::AccelerationStructureBuildRangeInfoKHR(prim->mIndexCount/3));
			blasBarriers.emplace_back(
				vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
				**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());

			it = mAccelerationStructures.emplace(key, BLAS { as, triangles.maxVertex, 
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
		TransformData& prevTransform = mTransformHistory[prim.get()];

		if (prim->mMaterial->mEmission.any()) {
			//LightData light;
			//light.mLightToWorld = transform;
			//light.mType = LIGHT_TYPE_EMISSIVE_MATERIAL;
			//light.mEmission = prim->mMaterial->mEmission;
			//*reinterpret_cast<uint32_t*>(&light.mCosInnerAngle) = prim->mFirstIndex;
			//*reinterpret_cast<uint32_t*>(&light.mCosOuterAngle) = prim->mIndexCount/3;
			//*reinterpret_cast<uint32_t*>(&light.mShadowBias) = prim->mMaterial->mEmissionImage;
			//light.mShadowIndex = (uint32_t)instanceDatas.size();
			//lights.emplace_back(light);
		}

		instanceDatas.emplace_back(transform, prevTransform, materialMap_it->second, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride());
		instanceBLAS.emplace_back(&it->second);
		totalVertexCount += it->second.mVertexCount;
		totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);
		prevTransform = transform;

		vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
		Eigen::Map<Eigen::Matrix<float, 3, 4, Eigen::RowMajor>>(&instance.transform.matrix[0][0]) = (Eigen::Translation3f(transform.mTranslation[0], transform.mTranslation[1], transform.mTranslation[2]) *
			Eigen::Quaternionf(transform.mRotation.w, transform.mRotation.xyz[0], transform.mRotation.xyz[1], transform.mRotation.xyz[2]) *
			Eigen::Scaling(transform.mScale)).matrix().topRows(3);
		instance.instanceCustomIndex = (uint32_t)instances.size() - 1;
		instance.mask = 0xFF;
		instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));
	});

	Buffer::View<VertexData> vertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(VertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	Buffer::View<byte> indices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);
	{ // copy vertices and indices
		commandBuffer.hold_resource(vertices);
		commandBuffer.hold_resource(indices);
		mCopyVerticesPipeline->descriptor("gVertices") = vertices;
		for (uint32_t i = 0; i < instanceBLAS.size(); i++) {
		InstanceData& d = instanceDatas[i];
		BLAS& b = *instanceBLAS[i];

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
		commandBuffer.copy_buffer(instanceBLAS[i]->mIndices, Buffer::View<byte>(indices.buffer(), d.mIndexByteOffset, b.mIndices.size_bytes()));
			
		commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		commandBuffer.barrier(indices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}
	}

	{ // Build TLAS
		commandBuffer.barrier(blasBarriers, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);
		vk::AccelerationStructureGeometryKHR geometry(vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryInstancesDataKHR());
		if (!instances.empty()) {
			auto buf = make_shared<Buffer>(commandBuffer.mDevice, "TLAS instance buffer", sizeof(vk::AccelerationStructureInstanceKHR)*instances.size(), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
			memcpy(buf->data(), instances.data(), buf->size());
			commandBuffer.hold_resource(buf);
			geometry.geometry.instances.data = buf->device_address();
		}
		mTopLevel = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geometry, vk::AccelerationStructureBuildRangeInfoKHR((uint32_t)instances.size()));
		commandBuffer.barrier(commandBuffer.hold_resource(mTopLevel).buffer(),
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);
	}
	
	auto instanceBuf = make_shared<Buffer>(commandBuffer.mDevice, "InstanceDatas", sizeof(InstanceData)*max<size_t>(1, instanceDatas.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	auto materialBuf = make_shared<Buffer>(commandBuffer.mDevice, "MaterialInfos", sizeof(MaterialData)*max<size_t>(1, materials.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	memcpy(commandBuffer.hold_resource(instanceBuf).data(), instanceDatas.data(), sizeof(InstanceData)*instanceDatas.size());
	memcpy(commandBuffer.hold_resource(materialBuf).data(), materials.data(), sizeof(MaterialData)*materials.size());

	mTracePrimaryRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTracePrimaryRaysPipeline->descriptor("gInstances") = Buffer::View<InstanceData>(instanceBuf);
	mTracePrimaryRaysPipeline->descriptor("gVertices") = vertices;
	mTracePrimaryRaysPipeline->descriptor("gIndices") = indices;

	mTraceIndirectRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTraceIndirectRaysPipeline->descriptor("gInstances") = Buffer::View<InstanceData>(instanceBuf);
	mTraceIndirectRaysPipeline->descriptor("gMaterials") = Buffer::View<MaterialData>(materialBuf);
	mTraceIndirectRaysPipeline->descriptor("gVertices") = vertices;
	mTraceIndirectRaysPipeline->descriptor("gIndices") = indices;
	
	mGradientForwardProjectPipeline->descriptor("gInstances") = Buffer::View<InstanceData>(instanceBuf);
	mGradientForwardProjectPipeline->descriptor("gVertices") = mTraceIndirectRaysPipeline->descriptor("gVertices");
	mGradientForwardProjectPipeline->descriptor("gIndices") = mTraceIndirectRaysPipeline->descriptor("gIndices");
	
	//mTraceIndirectRaysPipeline->descriptor("gLights") = lights.buffer_view();
	//mTraceIndirectRaysPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();

	for (const auto&[image, index] : images)
		mTraceIndirectRaysPipeline->descriptor("gImages", index) = sampled_image_descriptor(image);
}

void RayTraceScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer) {
	ProfilerRegion ps("RayTraceScene::draw", commandBuffer);

	mCurFrame.mCameraToWorld = node_to_world(camera.node());
	mCurFrame.mProjection = camera->projection((float)colorBuffer.extent().height/(float)colorBuffer.extent().width);

	if (!mCurFrame.mRadiance || mCurFrame.mRadiance.extent() != colorBuffer.extent()) {
		vk::Extent3D gradExtent(colorBuffer.extent().width / gGradientDownsample, colorBuffer.extent().height / gGradientDownsample, 1);
		
		mCurFrame.mVisibility = make_shared<Image>(commandBuffer.mDevice, "gVisibility", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame.mRNGSeed = make_shared<Image>(commandBuffer.mDevice, "gRNGSeed", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame.mGradientPositions = make_shared<Image>(commandBuffer.mDevice, "gGradientPositions", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		
		mCurFrame.mRadiance = make_shared<Image>(commandBuffer.mDevice, "gSamples" , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame.mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , colorBuffer.extent(), vk::Format::eR16G16B16A16Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame.mNormal     = make_shared<Image>(commandBuffer.mDevice, "gNormal", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame.mZ          = make_shared<Image>(commandBuffer.mDevice, "gZ"       , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame.mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame.mAccumLength  = make_shared<Image>(commandBuffer.mDevice, "gAccumLength", colorBuffer.extent(), vk::Format::eR32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame.mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mPrevUV = make_shared<Image>(commandBuffer.mDevice, "prevUV", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mPing = make_shared<Image>(commandBuffer.mDevice, "pong", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mPong = make_shared<Image>(commandBuffer.mDevice, "ping", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		for (uint32_t i = 0; i < mDiffPing.size(); i++) {
			mDiffPing[i] = make_shared<Image>(commandBuffer.mDevice, "diff pong", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
			mDiffPong[i] = make_shared<Image>(commandBuffer.mDevice, "diff ping", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		}
		mColorHistory = make_shared<Image>(commandBuffer.mDevice, "color history", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mColorHistoryUnfiltered = make_shared<Image>(commandBuffer.mDevice, "unfiltered color history", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
	}

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
	cameraData.data()->gCameraToWorld = mCurFrame.mCameraToWorld;
	cameraData.data()->gProjection = mCurFrame.mProjection;
	cameraData.data()->gWorldToPrevCamera = inverse(mPrevFrame.mCameraToWorld);
	cameraData.data()->gPrevProjection = mPrevFrame.mProjection;

	{ // Primary rays
		mTracePrimaryRaysPipeline->descriptor("gCameraData") = cameraData;
		mTracePrimaryRaysPipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
		mTracePrimaryRaysPipeline->descriptor("gNormal") = storage_image_descriptor(mCurFrame.mNormal);
		mTracePrimaryRaysPipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);
		mTracePrimaryRaysPipeline->descriptor("gPrevUV") = storage_image_descriptor(mPrevUV);
		commandBuffer.bind_pipeline(mTracePrimaryRaysPipeline->get_pipeline());
		mTracePrimaryRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTracePrimaryRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame.mVisibility.extent());
	}

	commandBuffer.clear_color_image(mCurFrame.mRNGSeed, vk::ClearColorValue(array<float,4>{ 0, 0, 0, 0 }));
	commandBuffer.clear_color_image(mCurFrame.mGradientPositions, vk::ClearColorValue(array<float,4>{ 0, 0, 0, 0 }));
		
	if (mPrevFrame.mVisibility && mPrevFrame.mVisibility.extent() == mCurFrame.mVisibility.extent()) {
		// forward project
		mGradientForwardProjectPipeline->push_constant<TransformData>("gCameraToWorld") = mCurFrame.mCameraToWorld;
		mGradientForwardProjectPipeline->push_constant<ProjectionData>("gProjection") = mCurFrame.mProjection;
		mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		mGradientForwardProjectPipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
		mGradientForwardProjectPipeline->descriptor("gPrevVisibility") = storage_image_descriptor(mPrevFrame.mVisibility);
		mGradientForwardProjectPipeline->descriptor("gNormal") = storage_image_descriptor(mCurFrame.mNormal);
		mGradientForwardProjectPipeline->descriptor("gPrevNormal") = storage_image_descriptor(mPrevFrame.mNormal);
		mGradientForwardProjectPipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);
		mGradientForwardProjectPipeline->descriptor("gPrevZ") = storage_image_descriptor(mPrevFrame.mZ);
		mGradientForwardProjectPipeline->descriptor("gRNGSeed") = storage_image_descriptor(mCurFrame.mRNGSeed);
		mGradientForwardProjectPipeline->descriptor("gPrevRNGSeed") = storage_image_descriptor(mPrevFrame.mRNGSeed);
		mGradientForwardProjectPipeline->descriptor("gGradientSamples") = storage_image_descriptor(mCurFrame.mGradientPositions);
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline());
		mGradientForwardProjectPipeline->bind_descriptor_sets(commandBuffer);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame.mVisibility.extent().width/gGradientDownsample, mCurFrame.mVisibility.extent().height/gGradientDownsample);
	}
	
	{ // Indirect/shading rays
		mTraceIndirectRaysPipeline->descriptor("gCameraData") = cameraData;
		mTraceIndirectRaysPipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
		mTraceIndirectRaysPipeline->descriptor("gRNGSeed") = storage_image_descriptor(mCurFrame.mRNGSeed);
		mTraceIndirectRaysPipeline->descriptor("gRadiance") = storage_image_descriptor(mCurFrame.mRadiance);
		mTraceIndirectRaysPipeline->descriptor("gAlbedo") = storage_image_descriptor(mCurFrame.mAlbedo);
		mTraceIndirectRaysPipeline->push_constant<uint32_t>("gRandomSeed") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		commandBuffer.bind_pipeline(mTraceIndirectRaysPipeline->get_pipeline());
		mTraceIndirectRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTraceIndirectRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame.mRadiance.extent());
}

	if (mDenoise && mPrevFrame.mRadiance && mPrevFrame.mRadiance.extent() == mCurFrame.mRadiance.extent()) 
		a_svgf(commandBuffer, colorBuffer);	
	else
		commandBuffer.blit_image(mCurFrame.mRadiance, colorBuffer);

	{ // Tonemap
		mTonemapPipeline->descriptor("gColor") = storage_image_descriptor(colorBuffer);
		mTonemapPipeline->descriptor("gAlbedo") = storage_image_descriptor(mCurFrame.mAlbedo);
		mTonemapPipeline->descriptor("gGradientSamples") = storage_image_descriptor(mCurFrame.mGradientPositions);
		mTonemapPipeline->descriptor("gHistoryLength") = storage_image_descriptor(mCurFrame.mAccumLength);
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(colorBuffer.extent());
	}
	
	swap(mPrevFrame, mCurFrame);
}
void RayTraceScene::a_svgf(CommandBuffer& commandBuffer, const Image::View& colorBuffer) {
	ProfilerRegion ps("a-svgf", commandBuffer);

	float2 jitterOffset = float2::Zero();

	{ // create diff image
		mCreateGradientSamplesPipeline->descriptor("gOutput1") = storage_image_descriptor(mDiffPing[0]);
		mCreateGradientSamplesPipeline->descriptor("gOutput2") = storage_image_descriptor(mDiffPing[1]);
		mCreateGradientSamplesPipeline->descriptor("gRadiance") = storage_image_descriptor(mCurFrame.mRadiance);
		mCreateGradientSamplesPipeline->descriptor("gPrevRadiance") = storage_image_descriptor(mPrevFrame.mRadiance);
		mCreateGradientSamplesPipeline->descriptor("gGradientSamples") = storage_image_descriptor(mCurFrame.mGradientPositions);
		mCreateGradientSamplesPipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
		mCreateGradientSamplesPipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);

		commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline());
		mCreateGradientSamplesPipeline->bind_descriptor_sets(commandBuffer);
		mCreateGradientSamplesPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mDiffPing[0].extent());
	}
	{ // filter diff image
		commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline());
		for (int i = 0; i < mDiffAtrousIterations; i++) {
			mAtrousGradientPipeline->descriptor("gOutput1") = storage_image_descriptor(mDiffPong[0]);
			mAtrousGradientPipeline->descriptor("gOutput2") = storage_image_descriptor(mDiffPong[1]);
			mAtrousGradientPipeline->descriptor("gInput1") = storage_image_descriptor(mDiffPing[0]);
			mAtrousGradientPipeline->descriptor("gInput2") = storage_image_descriptor(mDiffPing[1]);

			mAtrousGradientPipeline->push_constant<uint32_t>("gIteration") = i;
			mAtrousGradientPipeline->push_constant<uint32_t>("gStepSize") = (1 << i);

			mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
			mAtrousGradientPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mDiffPing[0].extent());

			std::swap(mDiffPing, mDiffPong);
		}
	}

	{ // temporal accumulation
		//std::swap(mCurFrame.mAccumColor, mPrevFrame.mAccumColor);
		//std::swap(mCurFrame.mAccumMoments, mPrevFrame.mAccumMoments);
		//std::swap(mCurFrame.mAccumLength, mPrevFrame.mAccumLength);

		mTemporalAccumulationPipeline->descriptor("gAccumColor") = storage_image_descriptor(mCurFrame.mAccumColor);
		mTemporalAccumulationPipeline->descriptor("gAccumMoments") = storage_image_descriptor(mCurFrame.mAccumMoments);
		mTemporalAccumulationPipeline->descriptor("gAccumLength") = storage_image_descriptor(mCurFrame.mAccumLength);

		mTemporalAccumulationPipeline->descriptor("gColor") = storage_image_descriptor(mCurFrame.mRadiance);
		mTemporalAccumulationPipeline->descriptor("gPrevColor") = storage_image_descriptor(mColorHistory);
		mTemporalAccumulationPipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
		mTemporalAccumulationPipeline->descriptor("gPrevMoments") = storage_image_descriptor(mPrevFrame.mAccumMoments);
		mTemporalAccumulationPipeline->descriptor("gHistoryLength") = storage_image_descriptor(mPrevFrame.mAccumLength);
		mTemporalAccumulationPipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);
		mTemporalAccumulationPipeline->descriptor("gPrevZ") = storage_image_descriptor(mPrevFrame.mZ);
		mTemporalAccumulationPipeline->descriptor("gNormal") = storage_image_descriptor(mCurFrame.mNormal);
		mTemporalAccumulationPipeline->descriptor("gPrevNormal") = storage_image_descriptor(mPrevFrame.mNormal);
		mTemporalAccumulationPipeline->descriptor("gDiff") = storage_image_descriptor(mDiffPing[0]);
		mTemporalAccumulationPipeline->descriptor("gPrevUV") = storage_image_descriptor(mPrevUV);

		mTemporalAccumulationPipeline->push_constant<float2>("gJitterOffset") = jitterOffset;

		commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline());
		mTemporalAccumulationPipeline->bind_descriptor_sets(commandBuffer);
		mTemporalAccumulationPipeline->push_constants(commandBuffer);
 		commandBuffer.dispatch_over(mCurFrame.mAccumColor.extent());
	}

	commandBuffer.copy_image(mCurFrame.mRadiance, mColorHistoryUnfiltered);

	mEstimateVariancePipeline->descriptor("gOutput") = storage_image_descriptor(mPing);
	mEstimateVariancePipeline->descriptor("gInput") = storage_image_descriptor(mCurFrame.mAccumColor);
	mEstimateVariancePipeline->descriptor("gNormal") = storage_image_descriptor(mCurFrame.mNormal);
	mEstimateVariancePipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);
	mEstimateVariancePipeline->descriptor("gVisibility") = storage_image_descriptor(mCurFrame.mVisibility);
	mEstimateVariancePipeline->descriptor("gMoments") = storage_image_descriptor(mCurFrame.mAccumMoments);
	mEstimateVariancePipeline->descriptor("gHistoryLength") = storage_image_descriptor(mCurFrame.mAccumLength);

	commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
	mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
	mEstimateVariancePipeline->push_constants(commandBuffer);
	commandBuffer.dispatch_over(mPing.extent());

	// atrous
	if (mNumIterations == 0)
			commandBuffer.copy_image(mPing, mColorHistory);
	else {
		mAtrousPipeline->descriptor("gNormal") = storage_image_descriptor(mCurFrame.mNormal);
		mAtrousPipeline->descriptor("gZ") = storage_image_descriptor(mCurFrame.mZ);
		commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
		for (uint32_t i = 0; i < mNumIterations; i++) {
			if (i - 1 == mHistoryTap)
				commandBuffer.copy_image(mPing, mColorHistory);

			mAtrousPipeline->descriptor("gOutput") = storage_image_descriptor(mPong);
			mAtrousPipeline->descriptor("gInput") = storage_image_descriptor(mPing);
			mAtrousPipeline->push_constant<uint32_t>("gIteration") = i;
			mAtrousPipeline->push_constant<uint32_t>("gStepSize") = 1 << i;

			mAtrousPipeline->bind_descriptor_sets(commandBuffer);
			mAtrousPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mPing.extent());

			std::swap(mPing, mPong);
		}
	}

	commandBuffer.blit_image(mPing, colorBuffer);
}