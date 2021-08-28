#include "RasterScene.hpp"
#include <Core/Window.hpp>

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm;
using namespace stm::hlsl;

hlsl::TransformData RasterScene::node_to_world(const Node& node) {
	TransformData transform(float3(0,0,0), 1.f, make_quatf(0,0,0,1));
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<hlsl::TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

RasterScene::RasterScene(Node& node) : mNode(node) {
	const spirv_module_map& spirv = *mNode.node_graph().find_components<spirv_module_map>().front();

	auto sampler = make_shared<Sampler>(spirv.begin()->second->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	mGeometryPipeline= node.make_child("Geometry Pipeline").make_component<PipelineState>("Opaque Geometry", spirv.at("pbr_vs"), spirv.at("pbr_fs"));
	mGeometryPipeline->set_immutable_sampler("gSampler", sampler);
	mGeometryPipeline->set_immutable_sampler("gShadowSampler", make_shared<Sampler>(sampler->mDevice, "gShadowSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		0, true, 2, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));
	
	mBackgroundPipeline = node.make_child("Background Pipeline").make_component<PipelineState>("Background", spirv.at("basic_skybox_vs"), spirv.at("basic_skybox_fs"));
	mBackgroundPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mBackgroundPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eEqual;
	mBackgroundPipeline->set_immutable_sampler("gSampler", sampler);
	mBackgroundPipeline->specialization_constant("gTextureCount") = 1;
	mBackgroundPipeline->descriptor("gTextures") = sampled_texture_descriptor(Texture::View());

	mGizmos = make_unique<Gizmos>(sampler->mDevice, *this, node.make_child("Gizmo Pipeline").make_component<PipelineState>("Gizmos", spirv.at("basic_color_texture_vs"), spirv.at("basic_color_texture_fs")));
	mGizmos->mPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mGizmos->mPipeline->set_immutable_sampler("gSampler", sampler);

	mShadowNode = node.make_child("ShadowMap DynamicRenderPass").make_component<DynamicRenderPass>();
	auto& shadowPass = *mShadowNode->subpasses().emplace_back(make_shared<DynamicRenderPass::Subpass>(*mShadowNode, "shadowPass"));
	shadowPass.emplace_attachment("gShadowMap", AttachmentType::eDepthStencil, blend_mode_state(), vk::AttachmentDescription({},
		vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
		vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
		vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal));
	
	mNode.listen(shadowPass.OnDraw, [&](CommandBuffer& commandBuffer) {
		mNode.for_each_descendant<LightData>([&](auto light) {
			const auto& extent = commandBuffer.bound_framebuffer()->extent();
			vk::Viewport vp(light->mShadowST[2]*extent.width, light->mShadowST[3]*extent.height, light->mShadowST[0]*extent.width, light->mShadowST[1]*extent.height, 0, 1);
			commandBuffer->setViewport(0, vp);
			commandBuffer->setScissor(0, vk::Rect2D( { (int32_t)vp.x, (int32_t)vp.y }, { (uint32_t)ceilf(vp.width), (uint32_t)ceilf(vp.height) } ));
			mGeometryPipeline->push_constant<TransformData>("gWorldToCamera") = inverse(light->mLightToWorld);
			mGeometryPipeline->push_constant<ProjectionData>("gProjection") = light->mShadowProjection;
			mDrawData->draw(commandBuffer, false);
		});
	});
}

void RasterScene::load_gltf(CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("pbrRenderer::load_gltf", commandBuffer);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	string err, warn;
	if (
		(filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
		(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) )
		throw runtime_error(filename.string() + ": " + err);
	if (!warn.empty()) fprintf_color(ConsoleColor::eYellow, stderr, "%s: %s\n", filename.string().c_str(), warn.c_str());
	
	Device& device = commandBuffer.mDevice;

	vector<shared_ptr<Buffer>> buffers(model.buffers.size());
	vector<shared_ptr<Texture>> images(model.images.size());
	buffer_vector<MaterialData> materials(commandBuffer.mDevice, model.materials.size());
	vector<vector<MeshInstance>> meshes(model.meshes.size());

	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<byte> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst);
		Buffer::View<byte> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", dst.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(tmp.data(), buffer.data.data(), tmp.size());
		commandBuffer.copy_buffer(tmp, dst);
		return dst.buffer();
	});
	ranges::transform(model.images, images.begin(), [&](const tinygltf::Image& image) {
		Buffer::View<byte> pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(pixels.data(), image.image.data(), pixels.size_bytes());
		commandBuffer.barrier(vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, pixels);
		
		static const unordered_map<int, std::array<vk::Format,4>> formatMap {
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, 	{ vk::Format::eR8Unorm, vk::Format::eR8G8Unorm, vk::Format::eR8G8B8Unorm, vk::Format::eR8G8B8A8Unorm } },
			{ TINYGLTF_COMPONENT_TYPE_BYTE, 					{ vk::Format::eR8Snorm, vk::Format::eR8G8Snorm, vk::Format::eR8G8B8Snorm, vk::Format::eR8G8B8A8Snorm } },
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, { vk::Format::eR16Unorm, vk::Format::eR16G16Unorm, vk::Format::eR16G16B16Unorm, vk::Format::eR16G16B16A16Unorm } },
			{ TINYGLTF_COMPONENT_TYPE_SHORT, 					{ vk::Format::eR16Snorm, vk::Format::eR16G16Snorm, vk::Format::eR16G16B16Snorm, vk::Format::eR16G16B16A16Snorm } },
			{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, 	{ vk::Format::eR32Uint, vk::Format::eR32G32Uint, vk::Format::eR32G32B32Uint, vk::Format::eR32G32B32A32Uint } },
			{ TINYGLTF_COMPONENT_TYPE_INT, 						{ vk::Format::eR32Sint, vk::Format::eR32G32Sint, vk::Format::eR32G32B32Sint, vk::Format::eR32G32B32A32Sint } },
			{ TINYGLTF_COMPONENT_TYPE_FLOAT, 					{ vk::Format::eR32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat, vk::Format::eR32G32B32A32Sfloat } },
			{ TINYGLTF_COMPONENT_TYPE_DOUBLE, 				{ vk::Format::eR64Sfloat, vk::Format::eR64G64Sfloat, vk::Format::eR64G64B64Sfloat, vk::Format::eR64G64B64A64Sfloat } }
		};
				
		vk::Format fmt = formatMap.at(image.pixel_type).at(image.component - 1);
		
		auto tex = make_shared<Texture>(device, image.name, vk::Extent3D(image.width, image.height, 1), fmt, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.copy_buffer_to_image(pixels, tex);
		tex->generate_mip_maps(commandBuffer);
		return tex;
	});
	ranges::transform(model.materials, materials.begin(), [](const tinygltf::Material& material) {
		MaterialData m;
		m.mEmission = Map<const Array3d>(material.emissiveFactor.data()).cast<float>();
		m.mBaseColor = Map<const Array4d>(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
		m.mMetallic = (float)material.pbrMetallicRoughness.metallicFactor;
		m.mRoughness = (float)material.pbrMetallicRoughness.roughnessFactor;
		m.mNormalScale = (float)material.normalTexture.scale;
		m.mOcclusionScale = (float)material.occlusionTexture.strength;
		TextureIndices inds;
		inds.mBaseColor = material.pbrMetallicRoughness.baseColorTexture.index;
		inds.mNormal = material.normalTexture.index;
		inds.mEmission = material.emissiveTexture.index;
		inds.mMetallic = inds.mRoughness = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
		inds.mOcclusion = material.occlusionTexture.index;
		inds.mMetallicChannel = 0;
		inds.mRoughnessChannel = 1;
		inds.mOcclusionChannel = 0;
		m.mTextureIndices = pack_texture_indices(inds);
		return m;
	});
	for (uint32_t i = 0; i < model.meshes.size(); i++) {
		meshes[i].resize(model.meshes[i].primitives.size());
		for (uint32_t j = 0; j < model.meshes[i].primitives.size(); j++) {
			const tinygltf::Primitive& prim = model.meshes[i].primitives[j];

			shared_ptr<Geometry> geometry = make_shared<Geometry>();
			
			vk::PrimitiveTopology topology;
			switch (prim.mode) {
				case TINYGLTF_MODE_POINTS: 					topology = vk::PrimitiveTopology::ePointList; break;
				case TINYGLTF_MODE_LINE: 						topology = vk::PrimitiveTopology::eLineList; break;
				case TINYGLTF_MODE_LINE_LOOP: 			topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_LINE_STRIP: 			topology = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_TRIANGLES: 			topology = vk::PrimitiveTopology::eTriangleList; break;
				case TINYGLTF_MODE_TRIANGLE_STRIP: 	topology = vk::PrimitiveTopology::eTriangleStrip; break;
				case TINYGLTF_MODE_TRIANGLE_FAN: 		topology = vk::PrimitiveTopology::eTriangleFan; break;
			}
			
			for (const auto&[attribName,attribIndex] : prim.attributes) {
				const tinygltf::Accessor& accessor = model.accessors[attribIndex];

				static const unordered_map<int, unordered_map<int, vk::Format>> formatMap {
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_BYTE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR8Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR8G8Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR8G8B8Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR8G8B8A8Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_SHORT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR16Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR16G16Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR16G16B16Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR16G16B16A16Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Uint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Uint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Uint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Uint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_INT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sint },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sint },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sint },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sint },
					} },
					{ TINYGLTF_COMPONENT_TYPE_FLOAT, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR32Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR32G32Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR32G32B32Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR32G32B32A32Sfloat },
					} },
					{ TINYGLTF_COMPONENT_TYPE_DOUBLE, {
						{ TINYGLTF_TYPE_SCALAR, vk::Format::eR64Sfloat },
						{ TINYGLTF_TYPE_VEC2, 	vk::Format::eR64G64Sfloat },
						{ TINYGLTF_TYPE_VEC3, 	vk::Format::eR64G64B64Sfloat },
						{ TINYGLTF_TYPE_VEC4, 	vk::Format::eR64G64B64A64Sfloat },
					} }
				};
				vk::Format attributeFormat = formatMap.at(accessor.componentType).at(accessor.type);

				Geometry::AttributeType attributeType;
				uint32_t typeIndex = 0;
				// parse typename & typeindex
				{
					string typeName;
					typeName.resize(attribName.size());
					ranges::transform(attribName, typeName.begin(), [&](char c) { return tolower(c); });
					size_t c = typeName.find_first_of("0123456789");
					if (c != string::npos) {
						typeIndex = stoi(typeName.substr(c));
						typeName = typeName.substr(0, c);
					}
					if (typeName.back() == '_') typeName.pop_back();
					static const unordered_map<string, Geometry::AttributeType> semanticMap {
						{ "position", 	Geometry::AttributeType::ePosition },
						{ "normal", 		Geometry::AttributeType::eNormal },
						{ "tangent", 		Geometry::AttributeType::eTangent },
						{ "bitangent", 	Geometry::AttributeType::eBinormal },
						{ "texcoord", 	Geometry::AttributeType::eTexcoord },
						{ "color", 			Geometry::AttributeType::eColor },
						{ "psize", 			Geometry::AttributeType::ePointSize },
						{ "pointsize", 	Geometry::AttributeType::ePointSize }
					};
					attributeType = semanticMap.at(typeName);
				}
				
				auto& attribs = (*geometry)[attributeType];
				if (attribs.size() <= typeIndex) attribs.resize(typeIndex+1);
				const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
				attribs[typeIndex] = {
					Geometry::AttributeDescription(accessor.ByteStride(bv), attributeFormat, (uint32_t)accessor.byteOffset, vk::VertexInputRate::eVertex),
					Buffer::StrideView(buffers[bv.buffer], accessor.ByteStride(bv), bv.byteOffset, bv.byteLength) };
			}
		
			size_t indexStride = 0;
			vk::IndexType indexType;
			const auto& indicesAccessor = model.accessors[prim.indices];
			switch (indicesAccessor.componentType) {
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			case TINYGLTF_COMPONENT_TYPE_BYTE:
				indexStride = sizeof(uint8_t);
				indexType = vk::IndexType::eUint8EXT;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			case TINYGLTF_COMPONENT_TYPE_SHORT:
				indexStride = sizeof(uint16_t);
				indexType = vk::IndexType::eUint16;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			case TINYGLTF_COMPONENT_TYPE_INT:
				indexStride = sizeof(uint32_t);
				indexType = vk::IndexType::eUint32;
				break;
			}
			
			Mesh mesh(geometry, Buffer::StrideView(buffers[model.bufferViews[indicesAccessor.bufferView].buffer], indexStride, model.bufferViews[indicesAccessor.bufferView].byteOffset, model.bufferViews[indicesAccessor.bufferView].byteLength), topology);
			meshes[i][j] = MeshInstance(mesh, indicesAccessor.count, 0, (uint32_t)(indicesAccessor.byteOffset/indexStride), prim.material, 0);
		}
	}

	mGeometryPipeline->specialization_constant("gTextureCount") = max(1u, (uint32_t)images.size());
	for (uint32_t i = 0; i < images.size(); i++)
		mGeometryPipeline->descriptor("gTextures", i) = sampled_texture_descriptor(images[i]);
	if (images.empty())
		mGeometryPipeline->descriptor("gTextures") = sampled_texture_descriptor(mBlankTexture);
	mGeometryPipeline->descriptor("gMaterials") = commandBuffer.copy_buffer(materials, vk::BufferUsageFlagBits::eStorageBuffer);

	vector<Node*> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		const auto& node = model.nodes[n];
		Node& dst = mNode.make_child(node.name);
		nodes[n] = &dst;
		
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			auto transform = dst.make_component<TransformData>(float3::Zero(), 1.f, make_quatf(0,0,0,1));
			if (!node.translation.empty()) transform->Translation = Map<const Array3d>(node.translation.data()).cast<float>();
			if (!node.rotation.empty()) 	 transform->Rotation = make_quatf((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			if (!node.scale.empty()) 			 transform->Scale = (float)Map<const Vector3d>(node.scale.data()).norm();
		}

		if (node.mesh < model.meshes.size())
			for (uint32_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++)
				dst.make_child(model.meshes[node.mesh].name).make_component<MeshInstance>(meshes[node.mesh][i]);

		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			auto light = dst.make_child(l.name).make_component<LightData>();
			light->mEmission = (Map<const Array3d>(l.color.data()) * l.intensity).cast<float>();
			if (l.type == "directional") {
				light->mType = LightType_Distant;
				light->mShadowProjection = make_orthographic(16, 16, 0, 0, 512, true);
			} else if (l.type == "point") {
				light->mType = LightType_Point;
				light->mShadowProjection = make_perspective(numbers::pi_v<float>/2, 1, 0, 0, 128, true);
			} else if (l.type == "spot") {
				light->mType = LightType_Spot;
				double co = cos(l.spot.outerConeAngle);
				light->mSpotAngleScale = (float)(1/(cos(l.spot.innerConeAngle) - co));
				light->mSpotAngleOffset = -(float)(co * light->mSpotAngleScale);
				light->mShadowProjection = make_perspective((float)l.spot.outerConeAngle, 1, 0, 0, 128, true);
			}
			light->mFlags = LightFlags_Shadowmap;
			light->mShadowBias = .000001f;
			light->mShadowST = Vector4f(1,1,0,0);
		}

		if (node.camera != -1) {
			const tinygltf::Camera& cam = model.cameras[node.camera];
			if (cam.type == "perspective")
				dst.make_child(cam.name).make_component<Camera>(ProjectionMode_RightHanded|ProjectionMode_Perspective, (float)cam.perspective.zfar, (float)cam.perspective.yfov);
			else if (cam.type == "orthographic")
				dst.make_child(cam.name).make_component<Camera>(ProjectionMode_RightHanded, (float)cam.orthographic.zfar, (float)cam.orthographic.ymag);
		}
	}

	for (size_t i = 0; i < model.nodes.size(); i++)
		for (int c : model.nodes[i].children)
			nodes[c]->set_parent(*nodes[i]);

	cout << "Loaded " << filename << endl;
}

void RasterScene::pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer) {
	ProfilerRegion s("RasterScene::pre_render", commandBuffer);

	if (!mBlankTexture) {
		mBlankTexture = make_shared<Texture>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2, 1), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled);
		Buffer::View<uint32_t> staging = make_shared<Buffer>(commandBuffer.mDevice, "blank upload", 16, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_COPY);
		memset(staging.data(), ~(0u), staging.size_bytes());
		commandBuffer.barrier(vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead, staging);
		commandBuffer.copy_buffer_to_image(staging, mBlankTexture);
	}

	if (mDrawData) mDrawData->clear();
	else mDrawData = make_unique<DrawData>(*this);
	
	buffer_vector<TransformData> transforms(commandBuffer.mDevice);
	buffer_vector<LightData> lights(commandBuffer.mDevice);
	
	{
		ProfilerRegion s("Compute transforms");
		mNode.for_each_descendant<MeshInstance>([&](const component_ptr<MeshInstance>& instance) {
			instance->mInstanceIndex = (uint32_t)transforms.size();
			transforms.emplace_back(node_to_world(instance.node()));
			mDrawData->add_instance(instance);
		});
		mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& light) {
			light->mLightToWorld = node_to_world(light.node());
			lights.emplace_back(*light);
		});
	}
	if (!transforms.empty()) {
		Texture::View shadowMap;
		Descriptor& descriptor = mGeometryPipeline->descriptor("gShadowMap");
		if (descriptor.index() == 0) {
			Texture::View tex = get<Texture::View>(descriptor);
			if (tex) shadowMap = tex;
		}
		if (!shadowMap) {
			ProfilerRegion s("make gShadowMap");
			shadowMap = make_shared<Texture>(commandBuffer.mDevice, "gShadowMap", vk::Extent3D(2048,2048,1), vk::Format::eD32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
			mGeometryPipeline->descriptor("gShadowMap") = sampled_texture_descriptor(shadowMap);
		}
		mGeometryPipeline->descriptor("gTransforms") = commandBuffer.copy_buffer(transforms, vk::BufferUsageFlagBits::eStorageBuffer);
		mGeometryPipeline->descriptor("gLights") = commandBuffer.copy_buffer(lights, vk::BufferUsageFlagBits::eStorageBuffer);
		mGeometryPipeline->push_constant<uint32_t>("gLightCount") = (uint32_t)lights.size();
		mShadowNode->render(commandBuffer, { { "gShadowMap", { shadowMap, vk::ClearValue({1.f, 0}) } } });
	}

	mGizmos->clear();

	OnGizmos(*mGizmos);
	
	mGeometryPipeline->transition_images(commandBuffer);
	mBackgroundPipeline->transition_images(commandBuffer);
	mGizmos->pre_render(commandBuffer, *this);
}
void RasterScene::draw(CommandBuffer& commandBuffer, const component_ptr<Camera>& camera, bool doShading) const {
	const auto& framebuffer = commandBuffer.bound_framebuffer();
	auto worldToCamera = inverse(node_to_world(camera.node()));
	auto projection = (camera->mProjectionMode & ProjectionMode_Perspective) ?
		make_perspective(camera->mVerticalFoV, (float)framebuffer->extent().height/(float)framebuffer->extent().width, 0, 0, camera->mFar, camera->mProjectionMode&ProjectionMode_RightHanded) :
		make_orthographic(camera->mOrthographicHeight, (float)framebuffer->extent().height/(float)framebuffer->extent().width, 0, 0, camera->mFar, camera->mProjectionMode&ProjectionMode_RightHanded);

	mGeometryPipeline->push_constant<TransformData>("gWorldToCamera") = worldToCamera;
	mGeometryPipeline->push_constant<ProjectionData>("gProjection") = projection;
	mDrawData->draw(commandBuffer, doShading);

	if (doShading) {		
		mGizmos->mPipeline->push_constant<TransformData>("gWorldToCamera") = worldToCamera;
		mGizmos->mPipeline->push_constant<ProjectionData>("gProjection") = projection;
		mGizmos->draw(commandBuffer);

		if (get<Texture::View>(mBackgroundPipeline->descriptor("gTextures"))) {
			mBackgroundPipeline->push_constant<TransformData>("gWorldToCamera") = worldToCamera;
			mBackgroundPipeline->push_constant<ProjectionData>("gProjection") = projection;
			commandBuffer.bind_pipeline(mBackgroundPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), GeometryStateDescription(vk::PrimitiveTopology::eTriangleList)));
			mBackgroundPipeline->bind_descriptor_sets(commandBuffer);
			mBackgroundPipeline->push_constants(commandBuffer);
			commandBuffer->draw(6, 1, 0, 0);
		}
	}
}

void RasterScene::DrawData::clear() {
	for (auto&[geometry, instances] : mMeshInstances) instances.clear();
}
void RasterScene::DrawData::draw(CommandBuffer& commandBuffer, bool doShading) const {
	ProfilerRegion s("RasterScene::draw", commandBuffer);
	for (const auto&[geometryHash, instances] : mMeshInstances) {
		commandBuffer.bind_pipeline(
			mGeometryPipeline->get_pipeline(
				commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(),
				instances.begin()->second.front()->mMesh.description(*mGeometryPipeline->stage(vk::ShaderStageFlagBits::eVertex)),
				doShading ? vk::ShaderStageFlagBits::eAll : vk::ShaderStageFlagBits::eVertex ) );
		mGeometryPipeline->bind_descriptor_sets(commandBuffer);
		mGeometryPipeline->push_constants(commandBuffer);
		for (const auto&[materialIdx, instanceVec] : instances) {
			commandBuffer.push_constant("gMaterialIndex", materialIdx);
			for (const auto& instance : instanceVec) {
				instance->mMesh.bind(commandBuffer);
				commandBuffer->drawIndexed(instance->mIndexCount, 1, instance->mFirstIndex, instance->mFirstVertex, instance->mInstanceIndex);
			}
		}
	}
}

void RasterScene::Gizmos::clear() {
	mGizmos.clear();
	mVertices.clear();
	mIndices.clear();
}
void RasterScene::Gizmos::pre_render(CommandBuffer& commandBuffer, RasterScene& scene) {
	if (mIndices.empty()) return;

	mPipeline->specialization_constant("gTextureCount") = max(1u, (uint32_t)mTextures.size());
	if (mTextures.empty()) {
		Descriptor& descriptor = mPipeline->descriptor("gTextures", 0);
		if (descriptor.index() != 0 || !get<Texture::View>(descriptor))
			descriptor = sampled_texture_descriptor(scene.mBlankTexture);
	}
	mPipeline->transition_images(commandBuffer);

	mDrawIndices = commandBuffer.copy_buffer(mIndices, vk::BufferUsageFlagBits::eIndexBuffer);
	auto verts = commandBuffer.copy_buffer(mVertices, vk::BufferUsageFlagBits::eVertexBuffer);
	if (!mDrawGeometry) {
		mDrawGeometry = make_shared<Geometry>(Geometry::AttributeType::ePosition, Geometry::AttributeType::eTexcoord, Geometry::AttributeType::eColor);
		(*mDrawGeometry)[Geometry::AttributeType::ePosition][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32B32Sfloat,    offsetof(vertex_t, mPosition), vk::VertexInputRate::eVertex);
		(*mDrawGeometry)[Geometry::AttributeType::eTexcoord][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32Sfloat,       offsetof(vertex_t, mTexcoord), vk::VertexInputRate::eVertex);
		(*mDrawGeometry)[Geometry::AttributeType::eColor   ][0].first = Geometry::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32B32A32Sfloat, offsetof(vertex_t, mColor   ), vk::VertexInputRate::eVertex);
	}
	(*mDrawGeometry)[Geometry::AttributeType::ePosition][0].second = verts;
	(*mDrawGeometry)[Geometry::AttributeType::eTexcoord][0].second = verts;
	(*mDrawGeometry)[Geometry::AttributeType::eColor   ][0].second = verts;
}
void RasterScene::Gizmos::draw(CommandBuffer& commandBuffer) const {
	for (const Gizmo& g : mGizmos) {
		Mesh mesh(mDrawGeometry, mDrawIndices, g.mTopology);
		commandBuffer.bind_pipeline(mPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), mesh.description(*mPipeline->stage(vk::ShaderStageFlagBits::eVertex))));
		mPipeline->bind_descriptor_sets(commandBuffer);
		mPipeline->push_constants(commandBuffer);
		commandBuffer.push_constant("gTextureIndex", g.mTextureIndex);
		commandBuffer.push_constant("gTextureST", g.mTextureST);
		commandBuffer.push_constant("gColor", g.mColor);
		commandBuffer->drawIndexed(g.mIndexCount, 1, g.mFirstIndex, g.mFirstVertex, 0);
	}
}

void RasterScene::Gizmos::cube(const hlsl::TransformData& transform, const hlsl::float4& color) {

}
void RasterScene::Gizmos::sphere(const hlsl::TransformData& transform, const hlsl::float4& color) {

}
void RasterScene::Gizmos::quad(const hlsl::TransformData& transform, const hlsl::float4& color, const Texture::View& texture, const hlsl::float4& textureST) {
	uint32_t t = ~0;
	if (texture) {
		if (auto it = mTextures.find(texture); it != mTextures.end())
			t = it->second;
		else {
			t = (uint32_t)mTextures.size();
			mTextures.emplace(texture, t);
			mPipeline->descriptor("gTextures", t) = sampled_texture_descriptor(texture);
		}
	}
	mGizmos.emplace_back(6, (uint32_t)mVertices.size(), (uint32_t)mIndices.size(), color, textureST, t, vk::PrimitiveTopology::eTriangleList);

	mVertices.emplace_back(transform_point(transform, float3(-1,-1,0)), color, float2(0,0));
	mVertices.emplace_back(transform_point(transform, float3( 1,-1,0)), color, float2(1,0));
	mVertices.emplace_back(transform_point(transform, float3(-1, 1,0)), color, float2(0,1));
	mVertices.emplace_back(transform_point(transform, float3( 1, 1,0)), color, float2(1,1));
	mIndices.emplace_back(0);
	mIndices.emplace_back(1);
	mIndices.emplace_back(2);
	mIndices.emplace_back(1);
	mIndices.emplace_back(3);
	mIndices.emplace_back(2);
}