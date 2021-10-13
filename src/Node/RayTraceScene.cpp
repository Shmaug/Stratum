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
    draw(commandBuffer, app->main_camera(), framebuffer->at("colorBuffer"), framebuffer->at("depthBuffer"));
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
	mCopyVerticesPipeline = mNode.make_child("pbr_rt_copy_vertices").make_component<ComputePipelineState>("pbr_rt_copy_vertices", shaders.at("pbr_rt_copy_vertices"));
	mTracePipeline = mNode.make_child("pbr_rt_render").make_component<ComputePipelineState>("pbr_rt_render", shaders.at("pbr_rt_render"));
	mTracePipeline->set_immutable_sampler("gSampler", sampler);
	mTracePipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
}

void RayTraceScene::on_inspector_gui() {
	ImGui::InputScalar("Sample Count", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gSampleCount"));
	ImGui::InputScalar("Bounces", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gMaxBounces"));
	ImGui::InputScalar("Debug Mode", ImGuiDataType_U32, &mTracePipeline->specialization_constant("gDebugMode"));
	ImGui::CheckboxFlags("Background Importance Sampling", &mTracePipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_BG_IS);
}

void RayTraceScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RasterScene::update", commandBuffer);

	buffer_vector<LightData,16> lights(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	lights.reserve(1);
	mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
		light->mLightToWorld = node_to_world(light.node());
		lights.emplace_back(*light);
	});

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

	mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
		if (prim->mMesh->topology() != vk::PrimitiveTopology::eTriangleList) return;

		// build BLAS
		size_t key = hash_args(prim->mMesh.get(), prim->mFirstIndex, prim->mIndexCount);
		auto it = mAccelerationStructures.find(key);
		if (it == mAccelerationStructures.end()) {
			const auto& [vertexPosDesc, positions] = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];
			
			if (prim->mMesh->index_type() != vk::IndexType::eUint32 && prim->mMesh->index_type() != vk::IndexType::eUint16)
				return;

			// build BLAS
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
			m.mImageIndices = pack_image_indices(inds);
			m.mEmission = prim->mMaterial->mEmission;
			m.mMetallic = prim->mMaterial->mMetallic;
			m.mRoughness = prim->mMaterial->mRoughness;
			m.mAbsorption = prim->mMaterial->mAbsorption;
			m.mIndexOfRefraction = prim->mMaterial->mIndexOfRefraction;
			m.mTransmission = prim->mMaterial->mTransmission;
			m.mNormalScale = prim->mMaterial->mNormalScale;
			m.mOcclusionScale = prim->mMaterial->mOcclusionScale;
		}

		TransformData transform = node_to_world(prim.node());
		
		if (prim->mMaterial->mEmission.any()) {
			LightData light;
			light.mLightToWorld = transform;
			light.mType = LIGHT_TYPE_EMISSIVE_MATERIAL;
			light.mEmission = prim->mMaterial->mEmission;
			*reinterpret_cast<uint32_t*>(&light.mCosInnerAngle) = prim->mFirstIndex;
			*reinterpret_cast<uint32_t*>(&light.mCosOuterAngle) = prim->mIndexCount/3;
			*reinterpret_cast<uint32_t*>(&light.mShadowBias) = prim->mMaterial->mEmissionImage;
			light.mShadowIndex = (uint32_t)instanceDatas.size();
			lights.emplace_back(light);
		}

		instanceDatas.emplace_back(transform, materialMap_it->second, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride());
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
	Buffer::View<VertexData> vertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(VertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
	Buffer::View<byte> indices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY);
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
		auto buf = make_shared<Buffer>(commandBuffer.mDevice, "TLAS instance buffer", sizeof(vk::AccelerationStructureInstanceKHR)*instances.size(), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR|vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_UNKNOWN);
		vk::MemoryRequirements reqs = buf->mDevice->getBufferMemoryRequirements(**buf);
		reqs.alignment = align_up(reqs.alignment,16);
		buf->bind_memory(make_shared<Device::MemoryAllocation>(commandBuffer.mDevice, reqs, VMA_MEMORY_USAGE_CPU_TO_GPU));
		memcpy(buf->data(), instances.data(), buf->size());
		commandBuffer.hold_resource(buf);
		geometry.geometry.instances.data = buf->device_address();
	}
	mTopLevel = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geometry, vk::AccelerationStructureBuildRangeInfoKHR((uint32_t)instances.size()));
	commandBuffer.barrier(commandBuffer.hold_resource(mTopLevel).buffer(),
		vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
		vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);

	auto instanceBuf = make_shared<Buffer>(commandBuffer.mDevice, "InstanceDatas", sizeof(InstanceData)*max<size_t>(1, instanceDatas.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	auto materialBuf = make_shared<Buffer>(commandBuffer.mDevice, "MaterialInfos", sizeof(MaterialInfo)*max<size_t>(1, materials.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(instanceBuf->data(), instanceDatas.data(), sizeof(InstanceData)*instanceDatas.size());
	memcpy(materialBuf->data(), materials.data(), sizeof(MaterialInfo)*materials.size());

	commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	commandBuffer.barrier(indices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);

	mTracePipeline->descriptor("gScene") = **mTopLevel;
	mTracePipeline->descriptor("gInstances") = commandBuffer.hold_resource(Buffer::View<InstanceData>(instanceBuf));
	mTracePipeline->descriptor("gMaterials") = commandBuffer.hold_resource(Buffer::View<MaterialInfo>(materialBuf));
	mTracePipeline->descriptor("gVertices") = vertices;
	mTracePipeline->descriptor("gIndices") = indices;
	//mTracePipeline->descriptor("gLights") = lights.buffer_view();
	//mTracePipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();
	mTracePipeline->push_constant<uint32_t>("gRandomSeed") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();

	/*
	if (Descriptor& d = mTracePipeline->descriptor("gNoiseLUT"); d.index() != 0 || !get<Image::View>(d)) {
		const uint32_t sz = mTracePipeline->specialization_constant("gNoiseLUTSize");
		buffer_vector<uint16_t> pixels(commandBuffer.mDevice, sz*sz, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		random_device rd;
		default_random_engine e{rd()};
		uniform_int_distribution<uint16_t> dist(0, numeric_limits<uint16_t>::max());
		for (uint32_t i = 0; i < pixels.size(); i++)
			pixels[i] = dist(e);
		commandBuffer.barrier(pixels.buffer_view(), vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		//generate_blue_noise(M, sz);
		Image::View noiseImg = make_shared<Image>(commandBuffer.mDevice, "gNoiseLUT", vk::Extent3D(sz, sz,1), vk::Format::eR16Unorm, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.copy_buffer_to_image(pixels.buffer_view(), noiseImg);
		mTracePipeline->descriptor("gNoiseLUT") = sampled_image_descriptor(noiseImg);
	}
	*/

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

	for (const auto&[image, index] : images)
		mTracePipeline->descriptor("gImages", index) = sampled_image_descriptor(image);
	
	mTracePipeline->transition_images(commandBuffer);
}

void RayTraceScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, const Image::View& colorBuffer, const Image::View& depthBuffer) const {
	ProfilerRegion ps("RayTraceScene::draw", commandBuffer);

	if (camera) {
		mTracePipeline->push_constant<TransformData>("gCameraToWorld") = node_to_world(camera.node());
		mTracePipeline->push_constant<ProjectionData>("gProjection") = camera->projection((float)colorBuffer.extent().height/(float)colorBuffer.extent().width);
	}

	mTracePipeline->push_constant<uint2>("gScreenResolution") = uint2(colorBuffer.extent().width, colorBuffer.extent().height);
	mTracePipeline->descriptor("gRenderTarget") = storage_image_descriptor(colorBuffer);

	commandBuffer.bind_pipeline(mTracePipeline->get_pipeline());
	mTracePipeline->bind_descriptor_sets(commandBuffer);
	mTracePipeline->push_constants(commandBuffer);
	commandBuffer.dispatch_over(colorBuffer.extent());
}