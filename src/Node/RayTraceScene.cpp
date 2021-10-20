#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#include "../Common/forced-random-dither.hpp"

using namespace stm;
using namespace stm::hlsl;

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

	auto sampler = make_shared<Sampler>(instance->device(), "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();
	if (mCopyVerticesPipeline) mNode.node_graph().erase(mCopyVerticesPipeline.node());
	if (mTracePipeline) mNode.node_graph().erase(mTracePipeline.node());
	mCopyVerticesPipeline = mNode.make_child("copy_vertices_copy").make_component<ComputePipelineState>("copy_vertices_copy", shaders.at("copy_vertices_copy"));
	
	mTracePipeline = mNode.make_child("pathtrace").make_component<ComputePipelineState>("pathtrace", shaders.at("pathtrace"));
	mTracePipeline->set_immutable_sampler("gSampler", sampler);
	mTracePipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	
	mReprojectPipeline = mNode.make_child("accumulate").make_component<ComputePipelineState>("accumulate", shaders.at("accumulate"));
	mReprojectPipeline->push_constant<float>("gExposure") = 1.f;
	mReprojectPipeline->push_constant<float>("gGamma") = 2.2f;
	
	mAtrousPipeline = mNode.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
}

void RayTraceScene::on_inspector_gui() {
	ImGui::InputScalar("Sample Count", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gSampleCount"));
	ImGui::InputScalar("Bounces", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gMaxBounces"));
	ImGui::InputScalar("Debug Mode", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gDebugMode"));
	ImGui::DragFloat("Exposure", &mReprojectPipeline->push_constant<float>("gExposure"));
	ImGui::DragFloat("Gamma", &mReprojectPipeline->push_constant<float>("gGamma"));
	ImGui::CheckboxFlags("Background Importance Sampling", &mTracePipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_BG_IS);

	ImGui::Checkbox("Naive Reprojection", &mNaiveAccumulation);
	ImGui::Checkbox("Modulate Albedo", &mModulateAlbedo);
	ImGui::InputScalar("Iterations", ImGuiDataType_U32, &mNumIterations);
	ImGui::InputScalar("Filter Type", ImGuiDataType_U32, &mAtrousPipeline->specialization_constant("gFilterKernelType"));
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

	auto envMap = mNode.find_in_descendants<EnvironmentMap>();
	if (envMap) {
		mTracePipeline->descriptor("gEnvironmentConditionalDistribution") = sampled_image_descriptor(envMap->mConditionalDistribution);
		mTracePipeline->descriptor("gEnvironmentMarginalDistribution") = sampled_image_descriptor(envMap->mMarginalDistribution);
		mTracePipeline->specialization_constant("gEnvironmentMap") = find_image_index(envMap->mImage);
	} else {
		mTracePipeline->specialization_constant("gEnvironmentMap") = -1;
		Image::View blank = make_shared<Image>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2,1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled);
		mTracePipeline->descriptor("gEnvironmentConditionalDistribution") = sampled_image_descriptor(blank);
		mTracePipeline->descriptor("gEnvironmentMarginalDistribution") = sampled_image_descriptor(blank);
	}

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
			ImageIndices inds;
			inds.mAlbedo = find_image_index(prim->mMaterial->mAlbedoImage);
			inds.mNormal = find_image_index(prim->mMaterial->mNormalImage);
			inds.mEmission = find_image_index(prim->mMaterial->mEmissionImage);
			inds.mMetallic = find_image_index(prim->mMaterial->mMetallicImage);
			inds.mRoughness = find_image_index(prim->mMaterial->mRoughnessImage);
			inds.mOcclusion = find_image_index(prim->mMaterial->mOcclusionImage);
			inds.mMetallicChannel = prim->mMaterial->mMetallicImageComponent;
			inds.mRoughnessChannel = prim->mMaterial->mRoughnessImageComponent;
			inds.mOcclusionChannel = prim->mMaterial->mOcclusionImageComponent;
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
			m.mImageIndices = pack_image_indices(inds);
		}

		TransformData transform = node_to_world(prim.node());
		TransformData prevTransform;
		if (auto it = mTransformHistory.find(prim.get()); it != mTransformHistory.end())
			prevTransform = it->second;
		else
			prevTransform = transform;
		mTransformHistory[prim.get()] = transform;
		
		//if (prim->mMaterial->mEmission.any()) {
		//	LightData light;
		//	light.mLightToWorld = transform;
		//	light.mType = LIGHT_TYPE_EMISSIVE_MATERIAL;
		//	light.mEmission = prim->mMaterial->mEmission;
		//	*reinterpret_cast<uint32_t*>(&light.mCosInnerAngle) = prim->mFirstIndex;
		//	*reinterpret_cast<uint32_t*>(&light.mCosOuterAngle) = prim->mIndexCount/3;
		//	*reinterpret_cast<uint32_t*>(&light.mShadowBias) = prim->mMaterial->mEmissionImage;
		//	light.mShadowIndex = (uint32_t)instanceDatas.size();
		//	lights.emplace_back(light);
		//}

		instanceDatas.emplace_back(transform, prevTransform, materialMap_it->second, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride());
		instanceBLAS.emplace_back(&it->second);
		totalVertexCount += it->second.mVertexCount;
		totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);

		vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
		Eigen::Map<Eigen::Matrix<float, 3, 4, Eigen::RowMajor>>(&instance.transform.matrix[0][0]) = (Eigen::Translation3f(transform.mTranslation[0], transform.mTranslation[1], transform.mTranslation[2]) *
			Eigen::Quaternionf(transform.mRotation.w, transform.mRotation.xyz[0], transform.mRotation.xyz[1], transform.mRotation.xyz[2]) *
			Eigen::Scaling(transform.mScale)).matrix().topRows(3);
		instance.instanceCustomIndex = (uint32_t)instances.size() - 1;
		instance.mask = 0xFF;
		instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));
	});
	// copy vertices and indices
	Buffer::View<VertexData> vertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(VertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	Buffer::View<byte> indices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);
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
	}

	// Build TLAS
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

	auto instanceBuf = make_shared<Buffer>(commandBuffer.mDevice, "InstanceDatas", sizeof(InstanceData)*max<size_t>(1, instanceDatas.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	auto materialBuf = make_shared<Buffer>(commandBuffer.mDevice, "MaterialInfos", sizeof(MaterialData)*max<size_t>(1, materials.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	memcpy(instanceBuf->data(), instanceDatas.data(), sizeof(InstanceData)*instanceDatas.size());
	memcpy(materialBuf->data(), materials.data(), sizeof(MaterialData)*materials.size());

	commandBuffer.hold_resource(instanceBuf);
	commandBuffer.hold_resource(materialBuf);

	commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	commandBuffer.barrier(indices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

	mTracePipeline->specialization_constant("gDemodulateAlbedo") = mModulateAlbedo;

	mTracePipeline->descriptor("gScene") = **mTopLevel;
	mTracePipeline->descriptor("gInstances") = Buffer::View<InstanceData>(instanceBuf);
	mTracePipeline->descriptor("gMaterials") = Buffer::View<MaterialData>(materialBuf);
	mTracePipeline->descriptor("gVertices") = vertices;
	mTracePipeline->descriptor("gIndices") = indices;
	//mTracePipeline->descriptor("gLights") = lights.buffer_view();
	//mTracePipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();
	mTracePipeline->push_constant<uint32_t>("gRandomSeed") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();

	for (const auto&[image, index] : images)
		mTracePipeline->descriptor("gImages", index) = sampled_image_descriptor(image);
	
	mTracePipeline->transition_images(commandBuffer);
}

void RayTraceScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer) const {
	ProfilerRegion ps("RayTraceScene::draw", commandBuffer);

	struct CameraData {
		TransformData gCameraToWorld;
		ProjectionData gProjection;
		TransformData gPrevCameraToWorld;
		ProjectionData gPrevProjection;
	};

	RTData cur,prev;
	if (mTracePipeline->descriptor("gSamples").index() == 0) {
		prev.mSamples  = get<Image::View>(mTracePipeline->descriptor("gSamples"));
		if (prev.mSamples) {
			Buffer::View<CameraData> prevCameraBuf = get<Buffer::StrideView>(mTracePipeline->descriptor("gCameraData"));
			prev.mCameraToWorld = prevCameraBuf.data()->gCameraToWorld;
			prev.mProjection = prevCameraBuf.data()->gProjection;
			prev.mNormalId = get<Image::View>(mTracePipeline->descriptor("gNormalId"));
			prev.mZ        = get<Image::View>(mTracePipeline->descriptor("gZ"));
			prev.mAlbedo   = get<Image::View>(mTracePipeline->descriptor("gAlbedo"));
		}
	}

	cur.mCameraToWorld = node_to_world(camera.node());
	cur.mProjection = camera->projection((float)colorBuffer.extent().height/(float)colorBuffer.extent().width);
	cur.mSamples  = make_shared<Image>(commandBuffer.mDevice, "gSamples" , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
	cur.mNormalId = make_shared<Image>(commandBuffer.mDevice, "gNormalId", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	cur.mZ        = make_shared<Image>(commandBuffer.mDevice, "gZ"       , colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	cur.mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , colorBuffer.extent(), vk::Format::eR8G8B8A8Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	Image::View prevUV   = make_shared<Image>(commandBuffer.mDevice, "gPrevUV", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	
	// trace rays

	Buffer::View<CameraData> cameraBuf = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	cameraBuf.data()->gCameraToWorld = cur.mCameraToWorld;
	cameraBuf.data()->gProjection = cur.mProjection;
	cameraBuf.data()->gPrevCameraToWorld = prev.mCameraToWorld;
	cameraBuf.data()->gPrevProjection = prev.mProjection;

	mTracePipeline->descriptor("gCameraData") = cameraBuf;
	mTracePipeline->descriptor("gSamples") = storage_image_descriptor(cur.mSamples);
	mTracePipeline->descriptor("gNormalId") = storage_image_descriptor(cur.mNormalId);
	mTracePipeline->descriptor("gZ") = storage_image_descriptor(cur.mZ);
	mTracePipeline->descriptor("gPrevUV") = storage_image_descriptor(prevUV);
	mTracePipeline->descriptor("gAlbedo") = storage_image_descriptor(cur.mAlbedo);

	commandBuffer.bind_pipeline(mTracePipeline->get_pipeline());
	mTracePipeline->bind_descriptor_sets(commandBuffer);
	mTracePipeline->push_constants(commandBuffer);
	commandBuffer.dispatch_over(cur.mSamples.extent());

	cur.mSamples.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
	cur.mNormalId.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
	cur.mZ.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
	cur.mAlbedo.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
	prevUV.transition_barrier(commandBuffer, vk::PipelineStageFlagBits::eComputeShader, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

	if (prev.mSamples && prev.mSamples.extent() == cur.mSamples.extent())
		denoise(commandBuffer, cur, prev, prevUV, colorBuffer);	
	else
		commandBuffer.blit_image(cur.mSamples, colorBuffer);
}
void RayTraceScene::denoise(CommandBuffer& commandBuffer, const RTData& cur, const RTData& prev, const Image::View& prevUV, const Image::View& colorBuffer) const {
	if (mNaiveAccumulation) {
		mReprojectPipeline->descriptor("gDst") = storage_image_descriptor(colorBuffer);
		mReprojectPipeline->descriptor("gSamples") = storage_image_descriptor(cur.mSamples);
		mReprojectPipeline->descriptor("gNormalId") = sampled_image_descriptor(cur.mNormalId);
		mReprojectPipeline->descriptor("gPrevSamples") = sampled_image_descriptor(prev.mSamples);
		mReprojectPipeline->descriptor("gPrevNormalId") = sampled_image_descriptor(prev.mNormalId);
		mReprojectPipeline->descriptor("gPrevUV") = sampled_image_descriptor(prevUV);
		commandBuffer.bind_pipeline(mReprojectPipeline->get_pipeline());
		mReprojectPipeline->bind_descriptor_sets(commandBuffer);
		mReprojectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(colorBuffer.extent());
	} else {
		/*
		ProfilerRegion ps("a-svgf", commandBuffer);

		float2 jitterOffset = float2::Zero();

		{
			ProfilerRegion ps("create_diff_image", commandBuffer);

			mCreateGradientSamplesPipeline->descriptor("gOutput1") = storage_image_descriptor(cur.mDiffPing[0]);
			mCreateGradientSamplesPipeline->descriptor("gOutput2") = storage_image_descriptor(cur.mDiffPing[1]);
			mCreateGradientSamplesPipeline->descriptor("gSamples") = sampled_image_descriptor(cur.mSamples);
			mCreateGradientSamplesPipeline->descriptor("gPrevSamples") = sampled_image_descriptor(prev.mSamples);
			mCreateGradientSamplesPipeline->descriptor("gGradientSamples") = sampled_image_descriptor(cur.mGradientPositions);
			mCreateGradientSamplesPipeline->descriptor("gNormalId") = sampled_image_descriptor(cur.mNormalId);
			mCreateGradientSamplesPipeline->descriptor("gZ") = sampled_image_descriptor(cur.mZ);

			commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline());
			mCreateGradientSamplesPipeline->bind_descriptor_sets(commandBuffer);
			mCreateGradientSamplesPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(cur.mDiffPing[0].extent());
		}

		{
			ProfilerRegion ps("filter_diff_image", commandBuffer);

			for (int i = 0; i < mDiffAtrousIterations; i++) {
				mAtrousGradientPipeline->descriptor("gInput1") = sampled_image_descriptor(cur.mDiffPing[0]);
				mAtrousGradientPipeline->descriptor("gInput2") = sampled_image_descriptor(cur.mDiffPing[1]);
				mAtrousGradientPipeline->descriptor("gOutput1") = sampled_image_descriptor(cur.mDiffPong[0]);
				mAtrousGradientPipeline->descriptor("gOutput2") = sampled_image_descriptor(cur.mDiffPong[1]);

				mAtrousGradientPipeline->push_constant<uint32_t>("gIteration") = i;
				mAtrousGradientPipeline->push_constant<uint32_t>("gStepSize") = (1 << i);

				commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline());
				mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
				mAtrousGradientPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(cur.mDiffPing[0].extent());

				for (uint32_t i = 0; i < cur.mDiffPing.size(); i++)
					std::swap(cur.mDiffPing[i], cur.mDiffPong[i]);
			}
		}

		{ // temporal accumulation
				ProfilerRegion ps("svgf_reimpl_temporal_accum", commandBuffer);
				std::swap(cur.mFBOAccum, prev.mFBOAccum);

				mTemporalAccumulationPipeline->descriptor("gOutput") = storage_image_descriptor(cur.mFBOAccum);
				mTemporalAccumulationPipeline->descriptor("gSamples") = sampled_image_descriptor(cur.mSamples);
				mTemporalAccumulationPipeline->descriptor("gPrevSamples") = sampled_image_descriptor(prev.mColorHistory);
				mTemporalAccumulationPipeline->descriptor("gPrevMoments") = sampled_image_descriptor(prev.mMoments);
				mTemporalAccumulationPipeline->descriptor("gHistoryLength") = sampled_image_descriptor(prev.mHistoryLength);
				mTemporalAccumulationPipeline->descriptor("gPrevUV") = sampled_image_descriptor(cur.mPrevUV);
				mTemporalAccumulationPipeline->descriptor("gZ") = sampled_image_descriptor(cur.mZ);
				mTemporalAccumulationPipeline->descriptor("gPrevZ") = sampled_image_descriptor(prev.mZ);
				mTemporalAccumulationPipeline->descriptor("gNormalId") = sampled_image_descriptor(cur.mNormalId);
				mTemporalAccumulationPipeline->descriptor("gPrevNormalId") = sampled_image_descriptor(prev.mNormalId);
				mTemporalAccumulationPipeline->descriptor("gGradientSamples") = sampled_image_descriptor(cur.mGradientPositions);
				mTemporalAccumulationPipeline->descriptor("gDiffCurrent") = sampled_image_descriptor(cur.mDiffPing[0]);
				mTemporalAccumulationPipeline->descriptor("gAntilagAlpha") = sampled_image_descriptor(cur.mAntilagAlpha);

				mTemporalAccumulationPipeline->push_constant<float2>("gJitterOffset") = jitterOffset;
				mTemporalAccumulationPipeline->push_constant<float>("gTemporalAlpha") = mTemporalAlpha;

				mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius") = mGradientFilterRadius;
				mTemporalAccumulationPipeline->specialization_constant("gShowAntilagAlpha") = mShowAntilagAlpha;

				commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline());
				mTemporalAccumulationPipeline->bind_descriptor_sets(commandBuffer);
				mTemporalAccumulationPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(cur.mDiffPing[0].extent());
		}

		{ // estimate variance
				ProfilerRegion ps("svgf_reimpl_estimate_variance", commandBuffer);
				mEstimateVariancePipeline->specialization_constant("gIterations") = mNumIterations;

				mEstimateVariancePipeline->descriptor("gOutput") = storage_image_descriptor(cur.mPing);
				mEstimateVariancePipeline->descriptor("gColor") = sampled_image_descriptor(cur.mAccumColor);
				mEstimateVariancePipeline->descriptor("gMoments") = sampled_image_descriptor(cur.mMoments);
				mEstimateVariancePipeline->descriptor("gHistoryLength") = sampled_image_descriptor(cur.mHistoryLength);
				mEstimateVariancePipeline->descriptor("gSamples") = sampled_image_descriptor(cur.mSamples);
				mEstimateVariancePipeline->descriptor("gZ") = sampled_image_descriptor(cur.mZ);
				mEstimateVariancePipeline->descriptor("gNormalId") = sampled_image_descriptor(cur.mNormalId);

				commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
				mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
				mEstimateVariancePipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(cur.mPing.extent());
		}

		commandBuffer.copy_image(cur.mSamples, cur.mColorHistoryUnfiltered);
		*/
		

		Image::View ping = make_shared<Image>(commandBuffer.mDevice, "pong", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		Image::View pong = make_shared<Image>(commandBuffer.mDevice, "ping", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.hold_resource(ping);
		commandBuffer.hold_resource(pong);
		
		commandBuffer.blit_image(cur.mSamples, ping);
		
		//if (mNumIterations == 0)
		//		commandBuffer.copy_image(ping, cur.mColorHistory);
		{ // atrous
			ProfilerRegion ps("svgf_reimpl_atrous", commandBuffer);

			mAtrousPipeline->descriptor("gZ") = sampled_image_descriptor(cur.mZ);
			mAtrousPipeline->descriptor("gNormalId") = sampled_image_descriptor(cur.mNormalId);
			mAtrousPipeline->descriptor("gAlbedo") = sampled_image_descriptor(cur.mAlbedo);
			for (int i = 0; i < mNumIterations; i++) {
				//if (i - 1 == mHistoryTap)
				//	commandBuffer.copy_image(ping, cur.mColorHistory);

				mAtrousPipeline->descriptor("gOutput") = storage_image_descriptor(pong);
				mAtrousPipeline->descriptor("gInput") = sampled_image_descriptor(ping);

				mAtrousPipeline->push_constant<uint32_t>("gIteration") = i;
				mAtrousPipeline->push_constant<uint32_t>("gStepSize") = 1 << i;
				mAtrousPipeline->specialization_constant("gModulateAlbedo") = mModulateAlbedo && (i == mNumIterations - 1);

				commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
				mAtrousPipeline->bind_descriptor_sets(commandBuffer);
				mAtrousPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(ping.extent());

				std::swap(ping, pong);
			}
		}

		commandBuffer.blit_image(ping, colorBuffer);
	}
}