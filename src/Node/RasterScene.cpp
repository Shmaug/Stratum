#include "RasterScene.hpp"
#include "Application.hpp"

using namespace stm;
using namespace stm::hlsl;

#pragma pack(push)
#pragma pack(1)
struct CameraData {
	TransformData gWorldToCamera;
	TransformData gCameraToWorld;
	ProjectionData gProjection;
};
#pragma pack(pop)

/*
inline void axis_arrows(DynamicGeometry& g, const TransformData& transform) {
	static const array<DynamicGeometry::vertex_t, 6> axisVertices {
		DynamicGeometry::vertex_t( float3(0,0,0), uchar4(0xFF,0,0,0xFF), float2(0,0) ),
		DynamicGeometry::vertex_t( float3(1,0,0), uchar4(0xFF,0,0,0xFF), float2(1,0) ),
		DynamicGeometry::vertex_t( float3(0,0,0), uchar4(0,0xFF,0,0xFF), float2(0,0) ),
		DynamicGeometry::vertex_t( float3(0,1,0), uchar4(0,0xFF,0,0xFF), float2(1,0) ),
		DynamicGeometry::vertex_t( float3(0,0,0), uchar4(0,0,0xFF,0xFF), float2(0,0) ),
		DynamicGeometry::vertex_t( float3(0,0,1), uchar4(0,0,0xFF,0xFF), float2(1,0) ),
	};
	static const array<uint16_t,6> axisIndices { 0,1, 2,3, 4,5 };
	g.add_instance(axisVertices, axisIndices, vk::PrimitiveTopology::eLineList, transform);
}
template<typename T> inline void gizmo_fn(DynamicGeometry&, const component_ptr<T>&) {}
template<> inline void gizmo_fn(DynamicGeometry& g, const component_ptr<LightData>& light) {
	axis_arrows(g, light->mLightToWorld);
}
template<> inline void gizmo_fn(DynamicGeometry& g, const component_ptr<TransformData>& transform) {
	axis_arrows(g, node_to_world(transform.node()));
}
*/

RasterScene::RasterScene(Node& node) : mNode(node) {
	mShadowPass = node.make_child("ShadowMap DynamicRenderPass").make_component<DynamicRenderPass>();
	auto& shadowPass = *mShadowPass->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mShadowPass, "shadowPass"));
	shadowPass.emplace_attachment("gShadowMap", AttachmentType::eDepthStencil, blend_mode_state(), vk::AttachmentDescription({},
		vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal));
	shadowPass.OnDraw.listen(mNode, [&](CommandBuffer& commandBuffer) { draw(commandBuffer, nullptr, false); });

	auto app = mNode.find_in_ancestor<Application>();
  app->OnUpdate.listen(mNode, bind(&RasterScene::update, this, std::placeholders::_1), EventPriority::eLast - 128);
  app->main_pass()->OnDraw.listen(mNode, [&,app](CommandBuffer& commandBuffer) { draw(commandBuffer, app->main_camera()); });

	create_pipelines();
}

void RasterScene::create_pipelines() {
	const ShaderDatabase& shaders = *mNode.node_graph().find_components<ShaderDatabase>().front();

	auto sampler = make_shared<Sampler>(shaders.begin()->second->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	if (mGeometryPipeline) mNode.node_graph().erase(mGeometryPipeline.node());
	mGeometryPipeline = mNode.make_child("VertexArrayObject Pipeline").make_component<GraphicsPipelineState>("Opaque VertexArrayObject", shaders.at("pbr_raster_vs"), shaders.at("pbr_raster_fs"));
	mGeometryPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eGreaterOrEqual;
	mGeometryPipeline->set_immutable_sampler("gSampler", sampler);
	mGeometryPipeline->set_immutable_sampler("gShadowSampler", make_shared<Sampler>(sampler->mDevice, "gShadowSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
		vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		0, true, 2, true, vk::CompareOp::eGreaterOrEqual, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatTransparentBlack)));
	mGeometryPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
	
	if (mBackgroundPipeline) mNode.node_graph().erase(mBackgroundPipeline.node());
	mBackgroundPipeline = mNode.make_child("Background Pipeline").make_component<GraphicsPipelineState>("Background", shaders.at("basic_skybox_vs"), shaders.at("basic_skybox_fs"));
	mBackgroundPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eGreaterOrEqual;
	mBackgroundPipeline->depth_stencil().depthWriteEnable = false;
	mBackgroundPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mBackgroundPipeline->set_immutable_sampler("gSampler", sampler);
	mBackgroundPipeline->descriptor_binding_flag("gImages", vk::DescriptorBindingFlagBits::ePartiallyBound);
}

void RasterScene::update(CommandBuffer& commandBuffer) {
	ProfilerRegion s("RasterScene::update", commandBuffer);

	unordered_map<Image::View, uint32_t> images;
	auto find_image_index = [&](Image::View image) -> uint32_t {
		if (!image) return ~0u;
		auto it = images.find(image);
		return (it == images.end()) ? images.emplace(image, (uint32_t)images.size()).first->second : it->second;
	};
	unordered_map<MaterialInfo*, uint32_t> materialMap;
	unordered_map<MaterialInfo*, unordered_map<size_t, vector<DrawCall>>> drawCalls;

	buffer_vector<LightData,16> lights(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	buffer_vector<MaterialData,16> materials(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);
	buffer_vector<TransformData,16> transforms(commandBuffer.mDevice, 0, vk::BufferUsageFlagBits::eStorageBuffer);

	uint32_t shadowViews = 0;

	mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
		PackedLightData p { light->mPackedData };
		if (p.shadow_index() != -1) {
			p.shadow_index(shadowViews);
			light->mPackedData = p.v;
			switch (light->mType) {
				case LIGHT_TYPE_SPOT: [[fallthrough]];
				case LIGHT_TYPE_DISTANT:
					shadowViews++;
					break;
				case LIGHT_TYPE_POINT:
					shadowViews += 6;
					break;
			}
		}
		light->mLightToWorld = node_to_world(light.node());
		light->mWorldToLight = inverse(light->mLightToWorld);
		lights.emplace_back(*light);
	});
	mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
		vector<DrawCall>& calls = drawCalls[prim->mMaterial.get()][prim->mMesh ? hash<VertexLayoutDescription>()(prim->mMesh->vertex_layout(*mGeometryPipeline->stage(vk::ShaderStageFlagBits::eVertex))) : 0];

		auto materialMap_it = materialMap.find(prim->mMaterial.get());
		if (materialMap_it == materialMap.end()) {
			materialMap_it = materialMap.emplace(prim->mMaterial.get(), (uint32_t)materials.size()).first;
			MaterialData& m = materials.emplace_back();
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
			m.mAlbedo = prim->mMaterial->mAlbedo;
			m.mImageIndices = inds.v;
			m.mEmission = prim->mMaterial->mEmission;
			m.mMetallic = prim->mMaterial->mMetallic;
			m.mRoughness = prim->mMaterial->mRoughness;
			m.mAbsorption = prim->mMaterial->mAbsorption;
			m.mIndexOfRefraction = prim->mMaterial->mIndexOfRefraction;
			m.mTransmission = prim->mMaterial->mTransmission;
			m.mNormalScale = prim->mMaterial->mNormalScale;
			m.mOcclusionScale = prim->mMaterial->mOcclusionScale;
		}
		
		DrawCall& drawCall = calls.emplace_back();
		drawCall.mMesh = prim->mMesh.get();
		drawCall.mMaterialIndex = materialMap_it->second;
		drawCall.mFirstIndex = prim->mFirstIndex;
		drawCall.mIndexCount = prim->mIndexCount;
		drawCall.mFirstInstance = (uint32_t)transforms.size();
		drawCall.mInstanceCount = 1;
		
		transforms.emplace_back(node_to_world(prim.node()));
	});
	
	auto envMap = mNode.find_in_descendants<EnvironmentMap>();
	if (envMap) {
		uint32_t idx = find_image_index(envMap->mImage);
		mGeometryPipeline->push_constant<uint32_t>("gEnvironmentMap") = idx;
		mBackgroundPipeline->push_constant<uint32_t>("gImageIndex") = idx;
		mBackgroundPipeline->descriptor("gImages", idx) = image_descriptor(envMap->mImage, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mBackgroundPipeline->push_constant<float>("gEnvironmentGamma") = envMap->mGamma;
		mBackgroundPipeline->transition_images(commandBuffer);
	} else {
		mBackgroundPipeline->descriptor("gImages") = {};
		mGeometryPipeline->push_constant<uint32_t>("gEnvironmentMap") = -1;
	}

	for (const auto&[image, index] : images)
		mGeometryPipeline->descriptor("gImages", index) = image_descriptor(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		
	if (!transforms.empty()) {
		mGeometryPipeline->descriptor("gMaterials") = materials.buffer_view();
		mGeometryPipeline->descriptor("gTransforms") = transforms.buffer_view();
	}
	
	mGeometryPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();
	if (lights.empty())
		mGeometryPipeline->descriptor("gLights") = Buffer::View<LightData>(make_shared<Buffer>(commandBuffer.mDevice, "gLights", sizeof(LightData), vk::BufferUsageFlagBits::eStorageBuffer));
	else
		mGeometryPipeline->descriptor("gLights") = lights.buffer_view();
	
	mDrawCalls.clear();
	for (const auto&[m, materialCalls] : drawCalls)
		for (const auto& [meshHash, calls] : materialCalls) {
			size_t sz = mDrawCalls.size();
			mDrawCalls.resize(sz + calls.size());
			memcpy(mDrawCalls.data() + sz, calls.data(), calls.size()*sizeof(DrawCall));
		}

	#pragma region Render shadows
	Image::View shadowMap;
	if (Descriptor& descriptor = mGeometryPipeline->descriptor("gShadowMaps"); descriptor.index() == 0) {
		Image::View tex = get<Image::View>(descriptor);
		if (tex) shadowMap = tex;
	}
	if (!shadowMap || shadowMap.image()->layer_count() < shadowViews) {
		ProfilerRegion s("make gShadowMaps");
		shadowMap = Image::View(
			make_shared<Image>(commandBuffer.mDevice, "gShadowMaps", vk::Extent3D(2048,2048,1), vk::Format::eD32Sfloat, max(1u,shadowViews), 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, VMA_MEMORY_USAGE_GPU_ONLY, vk::ImageCreateFlags{}, vk::ImageType::e2D),
			{}, {}, vk::ImageViewType::e2DArray);
		mGeometryPipeline->descriptor("gShadowMaps") = image_descriptor(shadowMap, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
	}

	if (!lights.empty()) {
		Buffer::View<CameraData> cameraDatas = make_shared<Buffer>(commandBuffer.mDevice, "gCameraDatas", lights.size()*6*sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		uint32_t cameraIndex = 0;
		mGeometryPipeline->descriptor("gCameraData") = cameraDatas;
		shadowMap.image()->transition_barrier(commandBuffer, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		for (const LightData& light : lights) {
			PackedLightData p { light.mPackedData };
			if (p.shadow_index() != -1) {
				if (light.mType == LIGHT_TYPE_POINT) {
					for (uint32_t i = 0; i < 6; i++) {
						TransformData t = light.mWorldToLight;
						float3 axis = float3::Zero();
						axis[i/2] = i%2==1 ? -1 : 1;
						t = tmul(t, make_transform(float3::Zero(), angle_axis(numbers::pi_v<float>/2, axis), float3::Ones()));
						cameraDatas[cameraIndex].gCameraToWorld = inverse(t);
						cameraDatas[cameraIndex].gWorldToCamera = t;
						cameraDatas[cameraIndex].gProjection = light.mShadowProjection;
						mShadowPass->render(commandBuffer, { { "gShadowMap", { Image::View(shadowMap.image(), 0, 1, p.shadow_index()+i, 1), vk::ClearValue({0.f, 0}) } } });
						cameraIndex++;
					}
				} else {
					cameraDatas[cameraIndex].gCameraToWorld = inverse(light.mWorldToLight);
					cameraDatas[cameraIndex].gWorldToCamera = light.mWorldToLight;
					cameraDatas[cameraIndex].gProjection = light.mShadowProjection;
					mShadowPass->render(commandBuffer, { { "gShadowMap", { Image::View(shadowMap.image(), 0, 1, p.shadow_index(), 1), vk::ClearValue({0.f, 0}) } } });
					cameraIndex++;
				}
			}
		}
	}
	#pragma endregion
	
	mGeometryPipeline->transition_images(commandBuffer);
}

void RasterScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading) const {
	ProfilerRegion ps("RasterScene::draw", commandBuffer);
	const auto& framebuffer = commandBuffer.bound_framebuffer();
	commandBuffer->setViewport(0, vk::Viewport(0, (float)framebuffer->extent().height, (float)framebuffer->extent().width, -(float)framebuffer->extent().height, 0, 1));
	commandBuffer->setScissor(0, vk::Rect2D({}, framebuffer->extent()));
	
	if (camera) {
		Buffer::View<CameraData> cameraData = make_shared<Buffer>(commandBuffer.mDevice, "gCameraData", sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
		cameraData.data()->gCameraToWorld = node_to_world(camera.node());
		cameraData.data()->gWorldToCamera = inverse(cameraData.data()->gCameraToWorld);
		cameraData.data()->gProjection = camera->projection((float)framebuffer->extent().height/(float)framebuffer->extent().width);
		mGeometryPipeline->descriptor("gCameraData") = cameraData;
		mBackgroundPipeline->descriptor("gCameraData") = cameraData;
	}

	const Mesh* lastMesh = nullptr;
	const GraphicsPipeline* lastPipeline = nullptr;
	uint32_t lastMaterial = -1;
	for (const DrawCall& drawCall : mDrawCalls) {
		if (drawCall.mMesh != lastMesh) {
			auto pipeline = mGeometryPipeline->get_pipeline(
					commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(),
					drawCall.mMesh->vertex_layout(*mGeometryPipeline->stage(vk::ShaderStageFlagBits::eVertex)),
					doShading ? vk::ShaderStageFlagBits::eAll : vk::ShaderStageFlagBits::eVertex );
			if (pipeline.get() != lastPipeline) {
				commandBuffer.bind_pipeline(pipeline);
				mGeometryPipeline->bind_descriptor_sets(commandBuffer);
				mGeometryPipeline->push_constants(commandBuffer);
				lastPipeline = pipeline.get();
			}
			drawCall.mMesh->bind(commandBuffer);
			lastMesh = drawCall.mMesh;
		}
		if (drawCall.mMaterialIndex != lastMaterial) {
			if (doShading) commandBuffer.push_constant("gMaterialIndex", drawCall.mMaterialIndex);
			lastMaterial = drawCall.mMaterialIndex;
		}
		commandBuffer->drawIndexed(drawCall.mIndexCount, drawCall.mInstanceCount, drawCall.mFirstIndex, 0, drawCall.mFirstInstance);
	}

	if (doShading && mGeometryPipeline->push_constant<uint32_t>("gEnvironmentMap") != -1) {
		commandBuffer.bind_pipeline(mBackgroundPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), VertexLayoutDescription(vk::PrimitiveTopology::eTriangleList)));
		mBackgroundPipeline->bind_descriptor_sets(commandBuffer);
		mBackgroundPipeline->push_constants(commandBuffer);
		commandBuffer->draw(6, 1, 0, 0);
	}
}
