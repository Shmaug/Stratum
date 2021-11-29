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

RayTraceScene::RayTraceScene(Node& node) : mNode(node), mCurFrame(make_unique<FrameData>()), mPrevFrame(make_unique<FrameData>()) {
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
		ImGui::PushItemWidth(40);
		ImGui::InputScalar("Bounces", ImGuiDataType_U32, &mTraceIndirectRaysPipeline->specialization_constant("gMaxBounces"));
		ImGui::PopItemWidth();
		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		ImGui::CheckboxFlags("Russian Roullette", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_RR);
		ImGui::CheckboxFlags("Importance Sample Background", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_BG_IS);
		ImGui::CheckboxFlags("Importance Sample Lights", &mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags"), SAMPLE_FLAG_LIGHT_IS);
		if (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") & (SAMPLE_FLAG_BG_IS|SAMPLE_FLAG_LIGHT_IS)) {
			uint32_t rs = (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") >> SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET)&0xFF;
			uint32_t pmax = 0xFF;
			if (ImGui::DragScalar("Reservoir Samples", ImGuiDataType_U32, &rs, 0, 0, &pmax)) {
				uint32_t mask = ~(0xFF << SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET);
				mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") = (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags")&mask) | ((rs&0xFF)<<SAMPLE_FLAG_RESERVOIR_SAMPLES_OFFSET);
			}
			if (rs > 0) {
				uint32_t rsh = (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") >> SAMPLE_FLAG_RESERVOIR_HISTORY_SAMPLES_OFFSET)&0xFF;
				if (ImGui::DragScalar("Reservoir History Samples", ImGuiDataType_U32, &rsh, 0, 0, &pmax)) {
					uint32_t mask = ~(0xFF << SAMPLE_FLAG_RESERVOIR_HISTORY_SAMPLES_OFFSET);
					mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags") = (mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags")&mask) | ((rsh&0xFF)<<SAMPLE_FLAG_RESERVOIR_HISTORY_SAMPLES_OFFSET);
				}
			}
		}
	}
	if (ImGui::CollapsingHeader("Denoising")) {
		ImGui::Checkbox("Forward projection", &mForwardProjection);
		ImGui::Checkbox("Reprojection", &mReprojection);
		if (mReprojection) {
			ImGui::SliderFloat("Temporal Alpha", &mTemporalAccumulationPipeline->push_constant<float>("gTemporalAlpha"), 0, 1);
			ImGui::PushItemWidth(40);
			ImGui::DragFloat("History Length Threshold", reinterpret_cast<float*>(&mEstimateVariancePipeline->specialization_constant("gHistoryLengthThreshold")), 1);
			if (mForwardProjection) {
				ImGui::InputScalar("Gradient Filter Iterations", ImGuiDataType_U32, &mDiffAtrousIterations);
				if (mDiffAtrousIterations > 0)
					ImGui::InputScalar("Gradient Filter Type", ImGuiDataType_U32, &mAtrousGradientPipeline->specialization_constant("gFilterKernelType"));
				ImGui::Checkbox("Enable Antilag", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gAntilag")));
				if (mTemporalAccumulationPipeline->specialization_constant("gAntilag")) {
					ImGui::DragFloat("Antilag Scale", &mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"), .01f, 0);
					ImGui::InputScalar("Antilag Radius", ImGuiDataType_U32, &mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius"));
				}
			}
			ImGui::PopItemWidth();
		}
		
		ImGui::PushItemWidth(40);
		ImGui::InputScalar("Filter Iterations", ImGuiDataType_U32, &mNumIterations);
		if (mNumIterations > 0) {
			ImGui::InputScalar("Filter Type", ImGuiDataType_U32, &mAtrousPipeline->specialization_constant("gFilterKernelType"));
			ImGui::InputScalar("History Tap Iteration", ImGuiDataType_U32, &mHistoryTap);
		}
		ImGui::PopItemWidth();
	}
	if (ImGui::CollapsingHeader("Post Processing")) {
		ImGui::PushItemWidth(40);
		ImGui::DragFloat("Exposure", &mTonemapPipeline->push_constant<float>("gExposure"), .1f, 0, 10);
		ImGui::DragFloat("Gamma", &mTonemapPipeline->push_constant<float>("gGamma"), .1f, 0, 5);
		ImGui::PopItemWidth();
	}
	ImGui::InputScalar("Debug Mode", ImGuiDataType_U32, &mTonemapPipeline->specialization_constant("gDebugMode"));
}

void RayTraceScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RayTraceScene::update", commandBuffer);

	swap(mPrevFrame, mCurFrame);

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
	auto find_image_index = [&](Image::View image) -> uint32_t {
		if (!image) return ~0u;
		auto it = images.find(image);
		return (it == images.end()) ? images.emplace(image, (uint32_t)images.size()).first->second : it->second;
	};

	vector<tuple<MeshPrimitive*, MeshAS*, uint32_t>> instanceIndices;
	vector<vk::AccelerationStructureInstanceKHR> instances;
	vector<InstanceData> instanceDatas;
	unordered_map<void*, pair<hlsl::TransformData, uint32_t>> transformHistory;
	Buffer::View<uint32_t> instanceIndexMap = make_shared<Buffer>(commandBuffer.mDevice, "InstanceIndexMap", sizeof(uint32_t)*max<size_t>(1, mTransformHistory.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memset(instanceIndexMap.data(), -1, instanceIndexMap.size_bytes());

	buffer_vector<LightData,16> lights(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	lights.reserve(1);

	{ // lights
		ProfilerRegion s("Process lights", commandBuffer);
		mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
			light->mLightToWorld = node_to_world(light.node());
			light->mWorldToLight = inverse(light->mLightToWorld);
			PackedLightData p { light->mPackedData };
			if ((light->mType == LIGHT_TYPE_POINT || light->mType == LIGHT_TYPE_SPOT) && p.radius() > 0) {
				p.instance_index((uint32_t)instanceDatas.size());
				light->mPackedData = p.v;
				
				TransformData prevTransform;
				if (auto it = mTransformHistory.find(light.get()); it != mTransformHistory.end()) {
					prevTransform = it->second.first;
					instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
				} else
					prevTransform = light->mLightToWorld;
					
				vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
				Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(tmul(light->mLightToWorld, make_transform(float3::Zero(), make_quatf(0,0,0,1), float3::Constant(p.radius()))));
				instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
				instance.mask = 0x2;
				instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**mUnitCubeAS);

				transformHistory.emplace(light.get(), make_pair(light->mLightToWorld, (uint32_t)instanceDatas.size()));
				instanceDatas.emplace_back(light->mLightToWorld, tmul(prevTransform, light->mWorldToLight), -1, -1, -1, (uint32_t)lights.size() << 8);
			}
			lights.emplace_back(*light);
		});
	}

	{ // find env map
		auto envMap = mNode.find_in_descendants<EnvironmentMap>();
		if (envMap && envMap->mImage) {
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = find_image_index(envMap->mImage);
			mTraceIndirectRaysPipeline->push_constant<float>("gEnvironmentMapGamma") = envMap->mGamma;
			mTraceIndirectRaysPipeline->push_constant<float>("gEnvironmentMapExposure") = envMap->mExposure;
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = image_descriptor(envMap->mConditionalDistribution, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = image_descriptor(envMap->mMarginalDistribution, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		} else {
			mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap") = -1;
			Image::View blank = make_shared<Image>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2,1), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = image_descriptor(blank, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
			mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = image_descriptor(blank, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		}
		mTracePrimaryRaysPipeline->specialization_constant("gEnvironmentMap") = mTraceIndirectRaysPipeline->specialization_constant("gEnvironmentMap");
		mTracePrimaryRaysPipeline->push_constant<float>("gEnvironmentMapGamma") = mTraceIndirectRaysPipeline->push_constant<float>("gEnvironmentMapGamma");
		mTracePrimaryRaysPipeline->push_constant<float>("gEnvironmentMapExposure") = mTraceIndirectRaysPipeline->push_constant<float>("gEnvironmentMapExposure");
		mTracePrimaryRaysPipeline->descriptor("gEnvironmentConditionalDistribution") = mTraceIndirectRaysPipeline->descriptor("gEnvironmentConditionalDistribution");
		mTracePrimaryRaysPipeline->descriptor("gEnvironmentMarginalDistribution") = mTraceIndirectRaysPipeline->descriptor("gEnvironmentMarginalDistribution");
	}

	{ // instances
		ProfilerRegion s("Process instances", commandBuffer);
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
				vk::GeometryFlagBitsKHR flag = {};
				if (prim->mMaterial->mAlphaCutoff == 0) flag = vk::GeometryFlagBitsKHR::eOpaque;
				vk::AccelerationStructureGeometryKHR triangleGeometry(vk::GeometryTypeKHR::eTriangles, triangles, flag);
				vk::AccelerationStructureBuildRangeInfoKHR range(prim->mIndexCount/3);
				auto as = make_shared<AccelerationStructure>(commandBuffer, prim.node().name()+"/BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, triangleGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());

				
				if (mMeshVertices.find(prim->mMesh.get()) == mMeshVertices.end()) {
					Buffer::View<VertexData>& vertices = mMeshVertices.emplace(prim->mMesh.get(),
						make_shared<Buffer>(commandBuffer.mDevice, prim.node().name()+"/VertexData", triangles.maxVertex*sizeof(VertexData), vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eShaderDeviceAddress)).first->second;
					
					// copy vertex data
					auto positions = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];
					auto normals = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eNormal)[0];
					auto tangents = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eTangent)[0];
					auto texcoords = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eTexcoord)[0];
					
					commandBuffer.bind_pipeline(mCopyVerticesPipeline->get_pipeline());
					mCopyVerticesPipeline->descriptor("gVertices") = vertices;
					mCopyVerticesPipeline->descriptor("gPositions") = Buffer::View(positions.second, positions.first.mOffset);
					mCopyVerticesPipeline->descriptor("gNormals")   = Buffer::View(normals.second, normals.first.mOffset);
					mCopyVerticesPipeline->descriptor("gTangents")  = Buffer::View(tangents.second, tangents.first.mOffset);
					mCopyVerticesPipeline->descriptor("gTexcoords") = Buffer::View(texcoords.second, texcoords.first.mOffset);
					mCopyVerticesPipeline->push_constant<uint32_t>("gCount") = vertices.size();
					mCopyVerticesPipeline->push_constant<uint32_t>("gPositionStride") = positions.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gNormalStride") = normals.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gTangentStride") = tangents.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gTexcoordStride") = texcoords.first.mStride;
					mCopyVerticesPipeline->bind_descriptor_sets(commandBuffer);
					mCopyVerticesPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(triangles.maxVertex);
					commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
				}

				it = mMeshAccelerationStructures.emplace(key, MeshAS { as, Buffer::StrideView(
						prim->mMesh->indices().buffer(),
						prim->mMesh->indices().stride(),
						prim->mMesh->indices().offset() + prim->mFirstIndex*prim->mMesh->indices().stride(),
						prim->mIndexCount*prim->mMesh->indices().stride()) }).first;
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
				m.mAlphaCutoff = prim->mMaterial->mAlphaCutoff;
				m.mImageIndices = inds.v;
			}
				
			TransformData transform = node_to_world(prim.node());
			TransformData prevTransform;
			if (auto transform_it = mTransformHistory.find(prim.get()); transform_it != mTransformHistory.end()) {
				prevTransform = transform_it->second.first;
				instanceIndexMap[transform_it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = transform;

			uint32_t lightIndex = -1;
			if ((prim->mMaterial->mEmission.array() > Array3f::Zero()).any()) {
				lightIndex = (uint32_t)lights.size();
				LightData light;
				light.mLightToWorld = transform;
				light.mType = LIGHT_TYPE_MESH;
				light.mEmission = prim->mMaterial->mEmission;
				for (uint32_t i = 0; i < prim->mIndexCount/3; i++) {
					PackedLightData p { float4::Zero() };
					p.instance_index((uint32_t)instanceDatas.size());
					p.prim_index(i);
					p.prim_count(1);
					light.mPackedData = p.v;
					lights.emplace_back(light);
				} 
			}

			vk::AccelerationStructureInstanceKHR& instance = instances.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = transform.m.topLeftCorner(3,4);
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = 0x1;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));

			instanceIndices.emplace_back(prim.get(), &it->second, (uint32_t)instanceDatas.size());
			transformHistory.emplace(prim.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
			instanceDatas.emplace_back(transform, tmul(prevTransform, inverse(transform)), materialMap_it->second, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride() | (lightIndex << 8));
			totalVertexCount += mMeshVertices.at(prim->mMesh.get()).size();
			totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);
		});
		mTransformHistory = transformHistory;
	}
	
	{ // Build TLAS
		ProfilerRegion s("Build TLAS", commandBuffer);
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

	if (!mCurFrame->mVertices || mCurFrame->mVertices.size() < totalVertexCount)
		mCurFrame->mVertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(VertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	if (!mCurFrame->mIndices || mCurFrame->mIndices.size() < totalIndexBufferSize)
		mCurFrame->mIndices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);
	if (!mCurFrame->mInstances || mCurFrame->mInstances.size() < instanceDatas.size())
		mCurFrame->mInstances = make_shared<Buffer>(commandBuffer.mDevice, "InstanceDatas", sizeof(InstanceData)*max<size_t>(1, instanceDatas.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mMaterials || mCurFrame->mMaterials.size() < materials.size())
		mCurFrame->mMaterials = make_shared<Buffer>(commandBuffer.mDevice, "MaterialInfos", sizeof(MaterialData)*max<size_t>(1, materials.size()), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	
	{ // copy vertices and indices
		ProfilerRegion s("Copy vertex data", commandBuffer);
		for (uint32_t i = 0; i < instanceIndices.size(); i++) {
			const auto&[prim, blas, instanceIndex] = instanceIndices[i];
			InstanceData& d = instanceDatas[instanceIndex];
			Buffer::View<VertexData>& meshVertices = mMeshVertices.at(prim->mMesh.get());
			commandBuffer.copy_buffer(meshVertices, Buffer::View<VertexData>(mCurFrame->mVertices.buffer(), d.mFirstVertex*sizeof(VertexData), meshVertices.size()));
			commandBuffer.copy_buffer(blas->mIndices, Buffer::View<byte>(mCurFrame->mIndices.buffer(), d.mIndexByteOffset, blas->mIndices.size_bytes()));
		}
		commandBuffer.barrier(mCurFrame->mIndices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		commandBuffer.barrier(mCurFrame->mVertices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}

	memcpy(mCurFrame->mInstances.data(), instanceDatas.data(), sizeof(InstanceData)*instanceDatas.size());
	memcpy(mCurFrame->mMaterials.data(), materials.data(), sizeof(MaterialData)*materials.size());

	mTracePrimaryRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTracePrimaryRaysPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mTracePrimaryRaysPipeline->descriptor("gMaterials") = mCurFrame->mMaterials;
	mTracePrimaryRaysPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mTracePrimaryRaysPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	mTracePrimaryRaysPipeline->descriptor("gLights") = lights.buffer_view();
	mTracePrimaryRaysPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();

	mGradientForwardProjectPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mGradientForwardProjectPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	mGradientForwardProjectPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mGradientForwardProjectPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	
	mTemporalAccumulationPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	
	mTraceIndirectRaysPipeline->descriptor("gScene") = **mTopLevel;
	mTraceIndirectRaysPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mTraceIndirectRaysPipeline->descriptor("gMaterials") = mCurFrame->mMaterials;
	mTraceIndirectRaysPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mTraceIndirectRaysPipeline->descriptor("gIndices") = mCurFrame->mIndices;
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
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != colorBuffer.extent()) {
		mCurFrame = make_unique<FrameData>();
		mCurFrame->mVisibility = make_shared<Image>(commandBuffer.mDevice, "gVisibility", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mRNGSeed = make_shared<Image>(commandBuffer.mDevice, "gRNGSeed", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mReservoirs = make_shared<Image>(commandBuffer.mDevice, "gReservoirs", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mReservoirRNG = make_shared<Image>(commandBuffer.mDevice, "gReservoirRNG", colorBuffer.extent(), vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);

		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance" , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mNormal   = make_shared<Image>(commandBuffer.mDevice, "gNormal", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mZ        = make_shared<Image>(commandBuffer.mDevice, "gZ"       , colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mAccumLength  = make_shared<Image>(commandBuffer.mDevice, "gAccumLength", colorBuffer.extent(), vk::Format::eR32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mPrevUV = make_shared<Image>(commandBuffer.mDevice, "gPrevUV", colorBuffer.extent(), vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mGradientPositions = make_shared<Image>(commandBuffer.mDevice, "gGradientPositions", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAntilagAlpha = make_shared<Image>(commandBuffer.mDevice, "gAntilagAlpha", colorBuffer.extent(), vk::Format::eR32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mPing = make_shared<Image>(commandBuffer.mDevice, "pong", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mPong = make_shared<Image>(commandBuffer.mDevice, "ping", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDiffPing[0] = make_shared<Image>(commandBuffer.mDevice, "diff ping 0", gradExtent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDiffPing[1] = make_shared<Image>(commandBuffer.mDevice, "diff ping 1", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDiffPong[0] = make_shared<Image>(commandBuffer.mDevice, "diff pong 0", gradExtent, vk::Format::eR32G32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mDiffPong[1] = make_shared<Image>(commandBuffer.mDevice, "diff pong 1", gradExtent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
	}
	if (!mColorHistory || mColorHistory.extent() != colorBuffer.extent())
		mColorHistory = make_shared<Image>(commandBuffer.mDevice, "Color history", colorBuffer.extent(), vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);

	bool hasHistory = mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	mCurFrame->mCameraToWorld = node_to_world(camera.node());
	mCurFrame->mProjection = camera->projection((float)colorBuffer.extent().height/(float)colorBuffer.extent().width);

	#pragma pack(push)
	#pragma pack(1)
	struct CameraData {
		TransformData gCameraToWorld;
		TransformData gWorldToCamera;
		ProjectionData gProjection;
		TransformData gWorldToPrevCamera;
		ProjectionData gPrevProjection;
	};
	#pragma pack(pop)
	Buffer::View<CameraData> cameraData = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	cameraData.data()->gCameraToWorld = mCurFrame->mCameraToWorld;
	cameraData.data()->gWorldToCamera = inverse(mCurFrame->mCameraToWorld);
	cameraData.data()->gProjection = mCurFrame->mProjection;
	cameraData.data()->gWorldToPrevCamera = hasHistory ? inverse(mPrevFrame->mCameraToWorld) : cameraData.data()->gWorldToCamera;
	cameraData.data()->gPrevProjection = hasHistory ? mPrevFrame->mProjection : mCurFrame->mProjection;

	{ // Primary rays
		ProfilerRegion ps("Primary rays", commandBuffer);
		mTracePrimaryRaysPipeline->descriptor("gCameraData") = cameraData;
		mTracePrimaryRaysPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gRNGSeed")    = image_descriptor(mCurFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		if (hasHistory) {
			mTracePrimaryRaysPipeline->descriptor("gPrevNormal") = image_descriptor(mPrevFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevZ") = image_descriptor(mPrevFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevReservoirs")   = image_descriptor(mPrevFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevReservoirRNG") = image_descriptor(mPrevFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		} else {
			mTracePrimaryRaysPipeline->descriptor("gPrevNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevReservoirs")   = image_descriptor(mCurFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTracePrimaryRaysPipeline->descriptor("gPrevReservoirRNG") = image_descriptor(mCurFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		}
		mTracePrimaryRaysPipeline->push_constant<uint32_t>("gHistoryValid") = hasHistory;
		mTracePrimaryRaysPipeline->descriptor("gPrevUV") = image_descriptor(mCurFrame->mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gReservoirs")   = image_descriptor(mCurFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTracePrimaryRaysPipeline->descriptor("gReservoirRNG") = image_descriptor(mCurFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		if (mRandomPerFrame) mTracePrimaryRaysPipeline->push_constant<uint32_t>("gRandomSeed") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		mTracePrimaryRaysPipeline->specialization_constant("gSamplingFlags") = mTraceIndirectRaysPipeline->specialization_constant("gSamplingFlags");
		commandBuffer.bind_pipeline(mTracePrimaryRaysPipeline->get_pipeline());
		mTracePrimaryRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTracePrimaryRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame->mVisibility.extent());
	}

	commandBuffer.clear_color_image(mCurFrame->mGradientPositions, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));

	if (mForwardProjection && hasHistory) {
		// forward project
		ProfilerRegion ps("Forward projection", commandBuffer);
		mGradientForwardProjectPipeline->push_constant<TransformData>("gWorldToCamera") = cameraData.data()->gWorldToCamera;
		mGradientForwardProjectPipeline->push_constant<ProjectionData>("gProjection") = mCurFrame->mProjection;
		if (mRandomPerFrame) mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = (uint32_t)commandBuffer.mDevice.mInstance.window().present_count();
		mGradientForwardProjectPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gRNGSeed") = image_descriptor(mCurFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gReservoirs") = image_descriptor(mCurFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gReservoirRNG") = image_descriptor(mCurFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gGradientSamples") = image_descriptor(mCurFrame->mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite|vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevUV") = image_descriptor(mCurFrame->mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mGradientForwardProjectPipeline->descriptor("gPrevVisibility") = image_descriptor(mPrevFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevNormal") = image_descriptor(mPrevFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevZ") = image_descriptor(mPrevFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevRNGSeed") = image_descriptor(mPrevFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevReservoirs") = image_descriptor(mPrevFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->descriptor("gPrevReservoirRNG") = image_descriptor(mPrevFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline());
		mGradientForwardProjectPipeline->bind_descriptor_sets(commandBuffer);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(gradExtent);
	}
	
	{ // Indirect/shading rays
		ProfilerRegion ps("Indirect rays", commandBuffer);
		mTraceIndirectRaysPipeline->descriptor("gCameraData") = cameraData;
		mTraceIndirectRaysPipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTraceIndirectRaysPipeline->descriptor("gRNGSeed")    = image_descriptor(mCurFrame->mRNGSeed, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTraceIndirectRaysPipeline->descriptor("gRadiance")   = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTraceIndirectRaysPipeline->descriptor("gAlbedo")     = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTraceIndirectRaysPipeline->descriptor("gReservoirs")   = image_descriptor(mCurFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTraceIndirectRaysPipeline->descriptor("gReservoirRNG") = image_descriptor(mCurFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mTraceIndirectRaysPipeline->get_pipeline());
		mTraceIndirectRaysPipeline->bind_descriptor_sets(commandBuffer);
		mTraceIndirectRaysPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(mCurFrame->mRadiance.extent());
	}

	Image::View tonemapInput = mCurFrame->mRadiance;

	if (hasHistory && mReprojection) {
		if (mForwardProjection) {
			{ // create diff image
				ProfilerRegion ps("Create diff image", commandBuffer);
				mCreateGradientSamplesPipeline->descriptor("gOutput1") = image_descriptor(mCurFrame->mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gOutput2") = image_descriptor(mCurFrame->mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gRadiance") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gPrevRadiance") = image_descriptor(mPrevFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gGradientPositions") = image_descriptor(mCurFrame->mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
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
					mAtrousGradientPipeline->descriptor("gOutput1") = image_descriptor(mCurFrame->mDiffPong[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
					mAtrousGradientPipeline->descriptor("gOutput2") = image_descriptor(mCurFrame->mDiffPong[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
					mAtrousGradientPipeline->descriptor("gInput1") = image_descriptor(mCurFrame->mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
					mAtrousGradientPipeline->descriptor("gInput2") = image_descriptor(mCurFrame->mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

					mAtrousGradientPipeline->push_constant<uint32_t>("gIteration") = i;
					mAtrousGradientPipeline->push_constant<uint32_t>("gStepSize") = (1 << i);

					mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
					mAtrousGradientPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(mCurFrame->mVisibility.extent().width/gGradientDownsample, mCurFrame->mVisibility.extent().height/gGradientDownsample);

					std::swap(mCurFrame->mDiffPing, mCurFrame->mDiffPong);
				}
			}
		}

		{ // temporal accumulation
			ProfilerRegion ps("Temporal accumulation", commandBuffer);
			mTemporalAccumulationPipeline->descriptor("gAccumColor") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAccumMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAccumLength") = image_descriptor(mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mTemporalAccumulationPipeline->descriptor("gAntilagAlpha") = image_descriptor(mCurFrame->mAntilagAlpha, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

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
			mTemporalAccumulationPipeline->descriptor("gDiff") = image_descriptor(mCurFrame->mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mTemporalAccumulationPipeline->descriptor("gPrevUV") = image_descriptor(mCurFrame->mPrevUV, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			commandBuffer.bind_pipeline(mTemporalAccumulationPipeline->get_pipeline());
			mTemporalAccumulationPipeline->bind_descriptor_sets(commandBuffer);
			mTemporalAccumulationPipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mCurFrame->mAccumColor.extent());
		}
		
		{ // estimate variance
			ProfilerRegion ps("Estimate Variance", commandBuffer);
			mEstimateVariancePipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mPing, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mEstimateVariancePipeline->descriptor("gInput") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gHistoryLength") = image_descriptor(mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

			commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
			mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
			mEstimateVariancePipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(mCurFrame->mPing.extent());
		}

		// atrous
		if (mHistoryTap >= mNumIterations)
			commandBuffer.copy_image(mCurFrame->mPing, mColorHistory);
		if (mNumIterations > 0) {
			ProfilerRegion ps("Filter image", commandBuffer);
			mAtrousPipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mAtrousPipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			commandBuffer.bind_pipeline(mAtrousPipeline->get_pipeline());
			for (uint32_t i = 0; i < mNumIterations; i++) {
				mAtrousPipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mPong, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mAtrousPipeline->descriptor("gInput") = image_descriptor(mCurFrame->mPing, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mAtrousPipeline->push_constant<uint32_t>("gIteration") = i;
				mAtrousPipeline->push_constant<uint32_t>("gStepSize") = 1 << i;

				mAtrousPipeline->bind_descriptor_sets(commandBuffer);
				mAtrousPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(mCurFrame->mPing.extent());

				if (i == mHistoryTap)
					commandBuffer.copy_image(mCurFrame->mPong, mColorHistory);

				std::swap(mCurFrame->mPing, mCurFrame->mPong);
			}
		}
		
		tonemapInput = mCurFrame->mPing;
	}

	{ // Tonemap
		ProfilerRegion ps("Tonemap", commandBuffer);
		mTonemapPipeline->descriptor("gInput") = image_descriptor(tonemapInput, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTonemapPipeline->descriptor("gOutput") = image_descriptor(colorBuffer, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTonemapPipeline->descriptor("gAlbedo") = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		uint32_t debugMode = mTonemapPipeline->specialization_constant("gDebugMode");
		mTonemapPipeline->descriptor("gDiff") = image_descriptor(mCurFrame->mDiffPing[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		switch (debugMode) {
		case 2:
			mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mDiffPing[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		case 5:
			mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		default:
		case 6:
			mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		}
		mTonemapPipeline->descriptor("gDebug2") = image_descriptor(debugMode == 3 ? mCurFrame->mAntilagAlpha : mCurFrame->mAccumLength, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		mTonemapPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(colorBuffer.extent());
	}
}