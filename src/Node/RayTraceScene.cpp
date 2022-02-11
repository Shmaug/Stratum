#include "RayTraceScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#include <stb_image_write.h>

#include <random>

using namespace stm::hlsl;

namespace stm {
	
namespace hlsl{
#include <HLSL/kernel/a-svgf/svgf_shared.hlsli>
#include <HLSL/tonemap.hlsli>
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

RayTraceScene::RayTraceScene(Node& node) : mNode(node), mCurFrame(make_unique<FrameData>()), mPrevFrame(make_unique<FrameData>()) {
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

	if (mTraceVisibilityPipeline)
		mNode.node_graph().erase_recurse(*mTraceVisibilityPipeline.node().parent());
	Node& n = mNode.make_child("pipelines");
	
	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();
	
	mCopyVerticesPipeline = n.make_child("copy_vertices").make_component<ComputePipelineState>("copy_vertices", shaders.at("copy_vertices"));
	
	mTraceVisibilityPipeline = n.make_child("pt_trace_visibility").make_component<ComputePipelineState>("pt_trace_visibility", shaders.at("pt_trace_visibility"));
	mTraceVisibilityPipeline->set_immutable_sampler("gSampler", samplerRepeat);
	mTraceVisibilityPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	
	mTraceIndirectPipeline = n.make_child("pt_trace_indirect").make_component<ComputePipelineState>("pt_trace_indirect", shaders.at("pt_trace_indirect"));
	mTraceIndirectPipeline->set_immutable_sampler("gSampler", samplerRepeat);
	mTraceIndirectPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	mTraceIndirectPipeline->push_constant<uint32_t>("gMinDepth") = 1;
	mTraceIndirectPipeline->push_constant<uint32_t>("gDirectLightDepth") = 3;
	mTraceIndirectPipeline->push_constant<uint32_t>("gSamplingFlags") = SAMPLE_FLAG_DEMODULATE_ALBEDO | SAMPLE_FLAG_BG_IS | SAMPLE_FLAG_LIGHT_IS;

	//mSpatialReusePipeline = n.make_child("pathtrace_spatial_reuse").make_component<ComputePipelineState>("pathtrace_spatial_reuse", shaders.at("pathtrace_spatial_reuse"));
	//mSpatialReusePipeline->set_immutable_sampler("gSampler", samplerRepeat);
	//mSpatialReusePipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);

	mTonemapPipeline = n.make_child("tonemap").make_component<ComputePipelineState>("tonemap", shaders.at("tonemap"));
	mTonemapPipeline->push_constant<float>("gExposure") = 1.f;

	mGradientForwardProjectPipeline = n.make_child("gradient_forward_project").make_component<ComputePipelineState>("gradient_forward_project", shaders.at("gradient_forward_project"));
	
	mCreateGradientSamplesPipeline = n.make_child("create_gradient_samples").make_component<ComputePipelineState>("create_gradient_samples", shaders.at("create_gradient_samples"));
	mAtrousGradientPipeline = n.make_child("atrous_gradient").make_component<ComputePipelineState>("atrous_gradient", shaders.at("atrous_gradient"));
	mTemporalAccumulationPipeline = n.make_child("temporal_accumulation").make_component<ComputePipelineState>("temporal_accumulation", shaders.at("temporal_accumulation"));
	mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit") = 128;
	mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale") = 1.f;
	mEstimateVariancePipeline = n.make_child("estimate_variance").make_component<ComputePipelineState>("estimate_variance", shaders.at("estimate_variance"));
	mAtrousPipeline = n.make_child("atrous").make_component<ComputePipelineState>("atrous", shaders.at("atrous"));
	mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost") = 3;
}

void RayTraceScene::on_inspector_gui() {
	if (ImGui::CollapsingHeader("Path Tracing")) {
		ImGui::PushItemWidth(40);
		ImGui::InputScalar("Sample Count", ImGuiDataType_U32, &mTraceIndirectPipeline->specialization_constant("gSampleCount"));
		ImGui::InputScalar("Max Depth", ImGuiDataType_U32, &mTraceIndirectPipeline->specialization_constant("gMaxDepth"));
		ImGui::InputScalar("Min Depth", ImGuiDataType_U32, &mTraceIndirectPipeline->push_constant<uint32_t>("gMinDepth"));
		ImGui::InputScalar("Direct Light Depth", ImGuiDataType_U32, &mTraceIndirectPipeline->push_constant<uint32_t>("gDirectLightDepth"));
		ImGui::PopItemWidth();

		ImGui::Checkbox("Random Frame Seed", &mRandomPerFrame);
		uint32_t& samplingFlags = mTraceIndirectPipeline->push_constant<uint32_t>("gSamplingFlags");
		ImGui::CheckboxFlags("Importance Sample Background", &samplingFlags, SAMPLE_FLAG_BG_IS);
		ImGui::CheckboxFlags("Importance Sample Lights", &samplingFlags, SAMPLE_FLAG_LIGHT_IS);
		if (samplingFlags & SAMPLE_FLAG_BG_IS) {
			ImGui::PushItemWidth(40);
			ImGui::InputFloat("Environment Sample Probability", reinterpret_cast<float*>(&mTraceIndirectPipeline->push_constant<float>("gEnvironmentSampleProbability")));
			ImGui::PopItemWidth();
		}
		ImGui::CheckboxFlags("Demodulate Albedo", &samplingFlags, SAMPLE_FLAG_DEMODULATE_ALBEDO);
	}

	if (ImGui::CollapsingHeader("Denoising")) {
		ImGui::Checkbox("Reprojection", &mReprojection);
		if (mReprojection) {
			ImGui::InputFloat("History Limit", &mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit"));
				
			ImGui::PushItemWidth(40);
			ImGui::DragFloat("History Length Threshold", reinterpret_cast<float*>(&mEstimateVariancePipeline->specialization_constant("gHistoryLengthThreshold")), 1);
			ImGui::InputScalar("Filter Iterations", ImGuiDataType_U32, &mAtrousIterations);
			if (mAtrousIterations > 0) {
				ImGui::Indent();
				ImGui::DragFloat("Sigma Luminance Boost", &mAtrousPipeline->push_constant<float>("gSigmaLuminanceBoost"), .01f, 0, 0, "%.2f");
				ImGui::InputScalar("Filter Type", ImGuiDataType_U32, &mAtrousPipeline->specialization_constant("gFilterKernelType"));
				ImGui::InputScalar("History Tap Iteration", ImGuiDataType_U32, &mHistoryTap);
				ImGui::Unindent();
			}
			
			ImGui::PopItemWidth();

			ImGui::Checkbox("Enable Antilag", reinterpret_cast<bool*>(&mTemporalAccumulationPipeline->specialization_constant("gAntilag")));
			if (mTemporalAccumulationPipeline->specialization_constant("gAntilag")) {
				ImGui::Indent();
				ImGui::PushItemWidth(40);
				ImGui::InputScalar("Gradient Filter Iterations", ImGuiDataType_U32, &mDiffAtrousIterations);
				if (mDiffAtrousIterations > 0)
					ImGui::InputScalar("Gradient Filter Type", ImGuiDataType_U32, &mAtrousGradientPipeline->specialization_constant("gFilterKernelType"));
				ImGui::DragFloat("Antilag Scale", &mTemporalAccumulationPipeline->push_constant<float>("gAntilagScale"), .01f, 0, 0, "%.1f");
				ImGui::InputScalar("Antilag Radius", ImGuiDataType_U32, &mTemporalAccumulationPipeline->specialization_constant("gGradientFilterRadius"));				
				ImGui::PopItemWidth();
				ImGui::Unindent();
			}
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
		ImGui::Checkbox("Gamma Correct", reinterpret_cast<bool*>(&mTonemapPipeline->specialization_constant("gGammaCorrection")));
		ImGui::PopItemWidth();
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
			d->waitIdle();
			Image::View img = mReprojection ? mPrevFrame->mAccumColor : mPrevFrame->mRadiance;
			Buffer::View<float> pixels = make_shared<Buffer>(d, "image copy tmp", img.extent().width*img.extent().height*sizeof(float)*4, vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_GPU_TO_CPU);
			
			auto cb = d.get_command_buffer("image copy", vk::QueueFlagBits::eCompute);
			cb->copy_image_to_buffer(img, pixels);
			d.submit(cb);
			cb->completion_fence().wait();

			stbi_write_hdr(path, img.extent().width, img.extent().height, 4, pixels.data());
		}
	}
}

void RayTraceScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RayTraceScene::update", commandBuffer);

	swap(mPrevFrame, mCurFrame);
	mCurFrame->mFrameId = mPrevFrame->mFrameId + 1;

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

	ImagePool images;
	images.distribution_data_size = 0;

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

	{ // environment map
		ProfilerRegion s("Process env map", commandBuffer);
		component_ptr<Material> envMap;
		mNode.for_each_descendant<Material>([&](component_ptr<Material> m) {
			if (m->index() == BSDFType::eEnvironment)
				envMap = m;
		});
		if (envMap) {
			uint32_t address = (uint32_t)(materialData.data.size()*sizeof(uint32_t));
			store_material(materialData, images, *envMap);
			mTraceIndirectPipeline->push_constant<uint32_t>("gEnvironmentMaterialAddress") = address;
			mTraceIndirectPipeline->push_constant<float>("gEnvironmentSampleProbability") = 0.5f;
		} else {
			mTraceIndirectPipeline->push_constant<uint32_t>("gEnvironmentMaterialAddress") = -1;
			mTraceIndirectPipeline->push_constant<float>("gEnvironmentSampleProbability") = 0;
		}
	}

	{ // spheres
		ProfilerRegion s("Process spheres", commandBuffer);
		mNode.for_each_descendant<SpherePrimitive>([&](const component_ptr<SpherePrimitive>& prim) {
			// append unique materials to materials list
			auto materialMap_it = materialMap.find(prim->mMaterial.get());
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, images, *prim->mMaterial);
			}
			if (prim->mMaterial->index() == BSDFType::eEmissive)
				lightInstances.emplace_back((uint32_t)instanceDatas.size());

			TransformData transform = node_to_world(prim.node());
			float r = prim->mRadius;
			#ifdef TRANSFORM_UNIFORM_SCALING
			r *= transform.mScale;
			transform.mScale = 1;
			#else
			const float3 scale = float3(
				transform.m.block(0, 0, 3, 1).matrix().norm(),
				transform.m.block(0, 1, 3, 1).matrix().norm(),
				transform.m.block(0, 2, 3, 1).matrix().norm() );
			const Quaternionf rotation(transform.m.block<3,3>(0,0).matrix() * DiagonalMatrix<float,3,3>(1/scale.x(), 1/scale.y(), 1/scale.z()));
			
			r *= transform.m.block<3,3>(0,0).matrix().determinant();
			transform = make_transform(transform.m.col(3).head<3>(), make_quatf(rotation.x(), rotation.y(), rotation.z(), rotation.w()), float3::Ones());
			#endif
			
			TransformData prevTransform;
			if (auto it = mTransformHistory.find(prim.get()); it != mTransformHistory.end()) {
				prevTransform = it->second.first;
				instanceIndexMap[it->second.second] = (uint32_t)instanceDatas.size();
			} else
				prevTransform = transform;

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Matrix<float,3,4,RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(tmul(transform, make_transform(float3::Zero(), quatf_identity(), float3::Constant(prim->mRadius))));
			instance.instanceCustomIndex = (uint32_t)instanceDatas.size();
			instance.mask = BVH_FLAG_SPHERES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**mUnitCubeAS);

			transformHistory.emplace(prim.get(), make_pair(transform, (uint32_t)instanceDatas.size()));
			instanceDatas.emplace_back( make_instance_sphere(transform, prevTransform, materialMap_it->second, r) );
		});
	}

	{ // meshes
		Material error_mat = Lambertian{};
		get<Lambertian>(error_mat).reflectance = make_image_value3({}, float3(1,0,1));
		materialMap.emplace(nullptr, (uint32_t)(materialData.data.size()*sizeof(uint32_t)));
		store_material(materialData, images, error_mat);

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
					commandBuffer.barrier(vertices, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
				}

				it = mMeshAccelerationStructures.emplace(prim->mMesh.get(), MeshAS { as, prim->mMesh->indices() }).first;
			}
			
			// append unique materials to materials list
			auto materialMap_it = materialMap.find(prim->mMaterial.get());
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)(materialData.data.size()*sizeof(uint32_t))).first;
				store_material(materialData, images, *prim->mMaterial);
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
		});
		mTransformHistory = transformHistory;
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
		mTopLevel = make_shared<AccelerationStructure>(commandBuffer, mNode.name()+"/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geom, range);
		commandBuffer.barrier(commandBuffer.hold_resource(mTopLevel).buffer(),
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);
	}

	if (!mCurFrame->mVertices || mCurFrame->mVertices.size() < totalVertexCount)
		mCurFrame->mVertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount,1u)*sizeof(PackedVertexData), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
	if (!mCurFrame->mIndices || mCurFrame->mIndices.size() < totalIndexBufferSize)
		mCurFrame->mIndices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize,1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);
	if (!mCurFrame->mInstances || mCurFrame->mInstances.size() < instanceDatas.size())
		mCurFrame->mInstances = make_shared<Buffer>(commandBuffer.mDevice, "gInstances", max<size_t>(1, instanceDatas.size())*sizeof(InstanceData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mMaterialData || mCurFrame->mMaterialData.size_bytes() < materialData.data.size()*sizeof(uint32_t))
		mCurFrame->mMaterialData = make_shared<Buffer>(commandBuffer.mDevice, "gMaterialData", max<size_t>(1, materialData.data.size())*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mLightInstances || mCurFrame->mLightInstances.size() < lightInstances.size())
		mCurFrame->mLightInstances = make_shared<Buffer>(commandBuffer.mDevice, "gLightInstances", max<size_t>(1, lightInstances.size())*sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mCurFrame->mDistributionData || mCurFrame->mDistributionData.size() < images.distribution_data_size)
		mCurFrame->mDistributionData = make_shared<Buffer>(commandBuffer.mDevice, "gDistributionData", max<size_t>(1, images.distribution_data_size)*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);

	{ // copy vertices and indices
		ProfilerRegion s("Copy vertex data", commandBuffer);
		for (uint32_t i = 0; i < instanceIndices.size(); i++) {
			const auto&[prim, blas, instanceIndex] = instanceIndices[i];
			const InstanceData& d = instanceDatas[instanceIndex];
			Buffer::View<PackedVertexData>& meshVertices = mMeshVertices.at(prim->mMesh.get());
			commandBuffer.copy_buffer(meshVertices, Buffer::View<PackedVertexData>(mCurFrame->mVertices.buffer(), d.first_vertex()*sizeof(PackedVertexData), meshVertices.size()));
			commandBuffer.copy_buffer(blas->mIndices, Buffer::View<byte>(mCurFrame->mIndices.buffer(), d.indices_byte_offset(), blas->mIndices.size_bytes()));
		}
		commandBuffer.barrier(mCurFrame->mIndices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		commandBuffer.barrier(mCurFrame->mVertices, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}

	memcpy(mCurFrame->mInstances.data(), instanceDatas.data(), instanceDatas.size()*sizeof(InstanceData));
	memcpy(mCurFrame->mMaterialData.data(), materialData.data.data(), materialData.data.size()*sizeof(uint32_t));
	memcpy(mCurFrame->mLightInstances.data(), lightInstances.data(), lightInstances.size()*sizeof(uint32_t));
	for (const auto&[buf, address] : images.distribution_data_map)
		commandBuffer.copy_buffer(buf, Buffer::View<float>(mCurFrame->mDistributionData, address, buf.size()));

	commandBuffer.barrier(mCurFrame->mDistributionData,
		vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite,
		vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead );

	mTraceVisibilityPipeline->descriptor("gScene") = **mTopLevel;
	mTraceVisibilityPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mTraceVisibilityPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	mTraceVisibilityPipeline->descriptor("gInstances") = mCurFrame->mInstances;

	mTraceIndirectPipeline->descriptor("gScene") = **mTopLevel;
	mTraceIndirectPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mTraceIndirectPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	mTraceIndirectPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mTraceIndirectPipeline->descriptor("gMaterialData") = mCurFrame->mMaterialData;
	mTraceIndirectPipeline->descriptor("gDistributions") = mCurFrame->mDistributionData;
	mTraceIndirectPipeline->descriptor("gLightInstances") = mCurFrame->mLightInstances;
	mTraceIndirectPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lightInstances.size();

	for (const auto&[image, index] : images.images)
		mTraceIndirectPipeline->descriptor("gImages", index) = image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
	
	mGradientForwardProjectPipeline->descriptor("gVertices") = mCurFrame->mVertices;
	mGradientForwardProjectPipeline->descriptor("gIndices") = mCurFrame->mIndices;
	mGradientForwardProjectPipeline->descriptor("gInstances") = mCurFrame->mInstances;
	mGradientForwardProjectPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;
	
	mTemporalAccumulationPipeline->descriptor("gInstanceIndexMap") = instanceIndexMap;

	mTonemapPipeline->specialization_constant("gModulateAlbedo") = mTraceIndirectPipeline->push_constant<uint32_t>("gSamplingFlags") & SAMPLE_FLAG_DEMODULATE_ALBEDO;
}

void RayTraceScene::render(CommandBuffer& commandBuffer, const Image::View& renderTarget, const vector<hlsl::ViewData>& views) {
	ProfilerRegion ps("RayTraceScene::render", commandBuffer);

	const vk::Extent3D extent = renderTarget.extent();
	const vk::Extent3D gradExtent((extent.width + gGradientDownsample-1) / gGradientDownsample, (extent.height + gGradientDownsample-1) / gGradientDownsample, 1);
	if (!mCurFrame->mRadiance || mCurFrame->mRadiance.extent() != extent) {
		mCurFrame = make_unique<FrameData>();
		for (Image::View& v : mCurFrame->mVisibility)
			v = make_shared<Image>(commandBuffer.mDevice, "gVisibility", extent, vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mRadiance = make_shared<Image>(commandBuffer.mDevice, "gRadiance", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc);
		mCurFrame->mAlbedo   = make_shared<Image>(commandBuffer.mDevice, "gAlbedo"  , extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		
		mCurFrame->mReservoirs = make_shared<Image>(commandBuffer.mDevice, "gReservoirs", extent, vk::Format::eR32G32B32A32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
		mCurFrame->mReservoirRNG = make_shared<Image>(commandBuffer.mDevice, "gReservoirRNG", extent, vk::Format::eR32G32B32A32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);

		mCurFrame->mAccumColor   = make_shared<Image>(commandBuffer.mDevice, "gAccumColor", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		mCurFrame->mAccumMoments = make_shared<Image>(commandBuffer.mDevice, "gAccumMoments", extent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled);
	
		mCurFrame->mGradientPositions = make_shared<Image>(commandBuffer.mDevice, "gGradientPositions", gradExtent, vk::Format::eR32Uint, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst);
		for (Image::View& v : mCurFrame->mTemp)
			v = make_shared<Image>(commandBuffer.mDevice, "pingpong", extent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		for (array<Image::View, 2>& v : mCurFrame->mDiffTemp) {
			v[0] = make_shared<Image>(commandBuffer.mDevice, "diff1 pingpong", gradExtent, vk::Format::eR16G16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
			v[1] = make_shared<Image>(commandBuffer.mDevice, "diff2 pingpong", gradExtent, vk::Format::eR16G16B16A16Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst);
		}
		
		mCurFrame->mFrameId = 0;
		commandBuffer.clear_color_image(mCurFrame->mAccumColor, vk::ClearColorValue{ array<float,4>{ 0.f, 0.f, 0.f, 0.f } });
	}
	
	const bool hasHistory = mPrevFrame->mRadiance && mPrevFrame->mRadiance.extent() == mCurFrame->mRadiance.extent();

	mCurFrame->mViews = make_shared<Buffer>(commandBuffer.mDevice, "gViews", views.size()*sizeof(hlsl::ViewData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);	
	memcpy(mCurFrame->mViews.data(), views.data(), mCurFrame->mViews.size_bytes());

	{ // Visibility
		ProfilerRegion ps("Visibility", commandBuffer);
		mTraceVisibilityPipeline->descriptor("gViews") = mCurFrame->mViews;
		mTraceVisibilityPipeline->descriptor("gPrevViews") = hasHistory ? mPrevFrame->mViews : mCurFrame->mViews;
		for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
			mTraceVisibilityPipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTraceVisibilityPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		mTraceVisibilityPipeline->push_constant<uint32_t>("gHistoryValid") = hasHistory;
		if (mRandomPerFrame) mTraceVisibilityPipeline->push_constant<uint32_t>("gRandomSeed") = rand();
		commandBuffer.bind_pipeline(mTraceVisibilityPipeline->get_pipeline());
		mTraceVisibilityPipeline->bind_descriptor_sets(commandBuffer);
		mTraceVisibilityPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	/*
	// Spatial reservoir re-use
	if (mSpatialReservoirIterations > 0) {
		ProfilerRegion ps("Spatial reservoir re-use", commandBuffer);
		mSpatialReusePipeline->specialization_constant("gSamplingFlags") = mTraceIndirectPipeline->specialization_constant("gSamplingFlags");
		mSpatialReusePipeline->descriptor("gViews") = mCurFrame->mViews;
		mSpatialReusePipeline->descriptor("gVisibility") = image_descriptor(mCurFrame->mVisibility, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mSpatialReusePipeline->descriptor("gNormal") = image_descriptor(mCurFrame->mNormal, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mSpatialReusePipeline->descriptor("gZ") = image_descriptor(mCurFrame->mZ, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mSpatialReusePipeline->descriptor("gReservoirs")   = image_descriptor(mCurFrame->mReservoirs, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mSpatialReusePipeline->descriptor("gReservoirRNG") = image_descriptor(mCurFrame->mReservoirRNG, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
		mSpatialReusePipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		commandBuffer.bind_pipeline(mSpatialReusePipeline->get_pipeline());
		mSpatialReusePipeline->bind_descriptor_sets(commandBuffer);
		for (uint32_t i = 0; i < mSpatialReservoirIterations; i++) {
			if (mRandomPerFrame) mSpatialReusePipeline->push_constant<uint32_t>("gRandomSeed") = rand();
			mSpatialReusePipeline->push_constants(commandBuffer);
			mSpatialReusePipeline->transition_images(commandBuffer);
			commandBuffer.dispatch_over(extent);
		}
	}
	*/

	commandBuffer.clear_color_image(mCurFrame->mGradientPositions, vk::ClearColorValue(array<uint32_t,4>{ 0, 0, 0, 0 }));

	if (mTemporalAccumulationPipeline->specialization_constant("gAntilag") && hasHistory) {
		// forward project
		ProfilerRegion ps("Forward projection", commandBuffer);
		mGradientForwardProjectPipeline->descriptor("gViews") = mCurFrame->mViews;
		for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++) {
			mGradientForwardProjectPipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			mGradientForwardProjectPipeline->descriptor("gPrevVisibility", i) = image_descriptor(mPrevFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		}
		mGradientForwardProjectPipeline->descriptor("gGradientSamples") = image_descriptor(mCurFrame->mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite|vk::AccessFlagBits::eShaderRead);
		mGradientForwardProjectPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		if (mRandomPerFrame) mGradientForwardProjectPipeline->push_constant<uint32_t>("gFrameNumber") = mCurFrame->mFrameId;
		commandBuffer.bind_pipeline(mGradientForwardProjectPipeline->get_pipeline());
		mGradientForwardProjectPipeline->bind_descriptor_sets(commandBuffer);
		mGradientForwardProjectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(gradExtent);
	}
	
	{ // Indirect
		ProfilerRegion ps("Indirect", commandBuffer);
		mTraceIndirectPipeline->descriptor("gViews") = mCurFrame->mViews;
		
		for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
			mTraceIndirectPipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		mTraceIndirectPipeline->descriptor("gRadiance") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		mTraceIndirectPipeline->descriptor("gAlbedo")   = image_descriptor(mCurFrame->mAlbedo, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

		mTraceIndirectPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
		commandBuffer.bind_pipeline(mTraceIndirectPipeline->get_pipeline());
		mTraceIndirectPipeline->bind_descriptor_sets(commandBuffer);
		mTraceIndirectPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(extent);
	}

	Image::View tonemap_in = mCurFrame->mRadiance;
	Image::View tonemap_out = mCurFrame->mTemp[0];

	if (hasHistory && mReprojection) {
		if (mTemporalAccumulationPipeline->specialization_constant("gAntilag")) {
			{ // create diff image
				ProfilerRegion ps("Create diff image", commandBuffer);
				mCreateGradientSamplesPipeline->descriptor("gViews")   = mCurFrame->mViews;
				mCreateGradientSamplesPipeline->descriptor("gOutput1") = image_descriptor(mCurFrame->mDiffTemp[0][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gOutput2") = image_descriptor(mCurFrame->mDiffTemp[0][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
				mCreateGradientSamplesPipeline->descriptor("gRadiance") = image_descriptor(mCurFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gPrevRadiance") = image_descriptor(mPrevFrame->mRadiance, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->descriptor("gGradientPositions") = image_descriptor(mCurFrame->mGradientPositions, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
						mCreateGradientSamplesPipeline->descriptor("gVisibility",i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
				mCreateGradientSamplesPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();

				commandBuffer.bind_pipeline(mCreateGradientSamplesPipeline->get_pipeline());
				mCreateGradientSamplesPipeline->bind_descriptor_sets(commandBuffer);
				mCreateGradientSamplesPipeline->push_constants(commandBuffer);
				commandBuffer.dispatch_over(gradExtent);
			}
			{ // filter diff image
				ProfilerRegion ps("Filter diff image", commandBuffer);
				mAtrousGradientPipeline->descriptor("gViews")   = mCurFrame->mViews;
				mAtrousGradientPipeline->descriptor("gImage1",0) = image_descriptor(mCurFrame->mDiffTemp[0][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage2",0) = image_descriptor(mCurFrame->mDiffTemp[0][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage1",1) = image_descriptor(mCurFrame->mDiffTemp[1][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				mAtrousGradientPipeline->descriptor("gImage2",1) = image_descriptor(mCurFrame->mDiffTemp[1][1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead|vk::AccessFlagBits::eShaderWrite);
				commandBuffer.bind_pipeline(mAtrousGradientPipeline->get_pipeline());
				mAtrousGradientPipeline->bind_descriptor_sets(commandBuffer);
				for (int i = 0; i < mDiffAtrousIterations; i++) {
					mAtrousGradientPipeline->push_constant<uint32_t>("gIteration") = i;
					mAtrousGradientPipeline->push_constant<uint32_t>("gStepSize") = (1 << i);
					mAtrousGradientPipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();
					mAtrousGradientPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(gradExtent);
					mAtrousGradientPipeline->transition_images(commandBuffer);
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
			commandBuffer.dispatch_over(extent);
			
			tonemap_in = mCurFrame->mAccumColor;
		}

		{ // estimate variance
			ProfilerRegion ps("Estimate Variance", commandBuffer);
			mEstimateVariancePipeline->descriptor("gViews") = mCurFrame->mViews;
			mEstimateVariancePipeline->descriptor("gInput") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gOutput") = image_descriptor(mCurFrame->mTemp[0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
			for (uint32_t i = 0; i < mCurFrame->mVisibility.size(); i++)
				mEstimateVariancePipeline->descriptor("gVisibility", i) = image_descriptor(mCurFrame->mVisibility[i], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->descriptor("gMoments") = image_descriptor(mCurFrame->mAccumMoments, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			mEstimateVariancePipeline->push_constant<uint32_t>("gViewCount") = (uint32_t)views.size();

			commandBuffer.bind_pipeline(mEstimateVariancePipeline->get_pipeline());
			mEstimateVariancePipeline->bind_descriptor_sets(commandBuffer);
			mEstimateVariancePipeline->push_constants(commandBuffer);
			commandBuffer.dispatch_over(extent);
			
			tonemap_in = mCurFrame->mTemp[0];
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
			for (uint32_t i = 0; i < mAtrousIterations; i++) {
				mAtrousPipeline->push_constant<uint32_t>("gIteration") = i;
				mAtrousPipeline->push_constant<uint32_t>("gStepSize") = 1 << i;
				mAtrousPipeline->push_constants(commandBuffer);
				mAtrousPipeline->transition_images(commandBuffer);
				commandBuffer.dispatch_over(extent);

				if (i+1 == mHistoryTap)
					commandBuffer.copy_image(mCurFrame->mTemp[(i+1)%2], mCurFrame->mAccumColor);
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
		switch (mTonemapPipeline->specialization_constant("gDebugMode")) {
		default:
		case DebugMode::eZ:
		case DebugMode::eDz:
		case DebugMode::eNormals:
			mTonemapPipeline->descriptor("gDebug3") = image_descriptor(mCurFrame->mVisibility[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		case DebugMode::ePrevUV:
			mTonemapPipeline->descriptor("gDebug2") = image_descriptor(mCurFrame->mVisibility[2], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		case DebugMode::eAccumLength:
			mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		case DebugMode::eAntilag:
			mTonemapPipeline->descriptor("gDebug2") = image_descriptor(mCurFrame->mDiffTemp[mDiffAtrousIterations%2][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
			break;
		}

		if (!get<Image::View>(mTonemapPipeline->descriptor("gDebug1"))) mTonemapPipeline->descriptor("gDebug1") = image_descriptor(mCurFrame->mAccumColor, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		if (!get<Image::View>(mTonemapPipeline->descriptor("gDebug2"))) mTonemapPipeline->descriptor("gDebug2") = image_descriptor(mCurFrame->mDiffTemp[mDiffAtrousIterations%2][0], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);
		if (!get<Image::View>(mTonemapPipeline->descriptor("gDebug3"))) mTonemapPipeline->descriptor("gDebug3") = image_descriptor(mCurFrame->mVisibility[1], vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderRead);

		commandBuffer.bind_pipeline(mTonemapPipeline->get_pipeline());
		mTonemapPipeline->bind_descriptor_sets(commandBuffer);
		if (mTonemapPipeline->specialization_constant("gDebugMode") == DebugMode::eAccumLength)
			commandBuffer.push_constant("gExposure", 1/mTemporalAccumulationPipeline->push_constant<float>("gHistoryLimit"));
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