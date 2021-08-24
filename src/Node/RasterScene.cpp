#include "RasterScene.hpp"
#include <Core/Window.hpp>

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm;
using namespace stm::hlsl;

hlsl::TransformData RasterScene::node_to_world(const Node& node) {
	TransformData transform(float3(0,0,0), 1.f, QUATF_I);
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<hlsl::TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

RasterScene::RasterScene(Node& node) : mNode(node), mGizmoVertices(mNode.find_in_ancestor<Instance>()->device()), mGizmoIndices(mNode.find_in_ancestor<Instance>()->device()) {
	const spirv_module_map& spirv = *mNode.node_graph().find_components<spirv_module_map>().front();

	auto sampler = make_shared<Sampler>(spirv.begin()->second->mDevice, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE));
	
	mGizmoPipeline = make_shared<PipelineState>("Gizmos", spirv.at("basic_color_texture_vs"), spirv.at("basic_color_texture_fs"));
	mGizmoPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mGizmoPipeline->set_immutable_sampler("gSampler", sampler);

	mGeometryPipeline= make_shared<PipelineState>("Opaque Geometry", spirv.at("pbr_vs"), spirv.at("pbr_fs"));
	mGeometryPipeline->set_immutable_sampler("gSampler", sampler);
	mGeometryPipeline->set_immutable_sampler("gShadowSampler", make_shared<Sampler>(sampler->mDevice, "gShadowSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		0, true, 2, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));
	
	mSkyboxPipeline = make_shared<PipelineState>("Skybox", spirv.at("basic_skybox_vs"), spirv.at("basic_skybox_fs"));
	mSkyboxPipeline->raster_state().cullMode = vk::CullModeFlagBits::eNone;
	mSkyboxPipeline->depth_stencil().depthCompareOp = vk::CompareOp::eEqual;
	mSkyboxPipeline->set_immutable_sampler("gSampler", sampler);

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
			mGeometryPipeline->push_constant("gWorldToCamera", inverse(light->mLightToWorld));
			mGeometryPipeline->push_constant("gProjection", light->mShadowProjection);
			draw(commandBuffer, vk::ShaderStageFlagBits::eVertex);
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
	vector<vector< shared_ptr<Geometry> >> geometries(model.meshes.size());

	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<byte> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst);
		Buffer::View<byte> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", dst.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(tmp.data(), buffer.data.data(), tmp.size());
		commandBuffer.copy_buffer(tmp, dst);
		return dst.buffer();
	});
	ranges::transform(model.images, images.begin(), [&](const tinygltf::Image& image) {
		auto pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		commandBuffer.hold_resource(pixels);
		memcpy(pixels->data(), image.image.data(), pixels->size());
		
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
		commandBuffer.upload_image(pixels, tex);
		tex->transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
		commandBuffer->copyBufferToImage(**pixels, **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->array_layers()), {}, tex->extent()) });
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
	ranges::transform(model.meshes, geometries.begin(), [&](const tinygltf::Mesh& mesh) {
		vector<shared_ptr<Geometry>> geoms(mesh.primitives.size());
		ranges::transform(mesh.primitives, geoms.begin(), [&](const tinygltf::Primitive& prim) {
			vk::PrimitiveTopology topo;
			switch (prim.mode) {
				case TINYGLTF_MODE_POINTS: 					topo = vk::PrimitiveTopology::ePointList; break;
				case TINYGLTF_MODE_LINE: 						topo = vk::PrimitiveTopology::eLineList; break;
				case TINYGLTF_MODE_LINE_LOOP: 			topo = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_LINE_STRIP: 			topo = vk::PrimitiveTopology::eLineStrip; break;
				case TINYGLTF_MODE_TRIANGLES: 			topo = vk::PrimitiveTopology::eTriangleList; break;
				case TINYGLTF_MODE_TRIANGLE_STRIP: 	topo = vk::PrimitiveTopology::eTriangleStrip; break;
				case TINYGLTF_MODE_TRIANGLE_FAN: 		topo = vk::PrimitiveTopology::eTriangleFan; break;
			}
			auto geometry = make_shared<Geometry>(topo);
			for (const auto&[attribName,attribIndex] : prim.attributes) {
				const auto& accessor = model.accessors[attribIndex];

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
				vk::Format fmt = formatMap.at(accessor.componentType).at(accessor.type);

				// parse typename & typeindex
				uint32_t typeIdx = 0;
				string typeName;
				typeName.resize(attribName.size());
				ranges::transform(attribName, typeName.begin(), [&](char c) { return tolower(c); });
				size_t c = typeName.find_first_of("0123456789");
				if (c != string::npos) {
					typeIdx = stoi(typeName.substr(c));
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
				auto& attribs = (*geometry)[semanticMap.at(typeName)];

				if (attribs.size() < typeIdx) attribs.resize(typeIdx);
				auto it = (attribs.size() > typeIdx) ? (attribs.begin() + typeIdx + 1) : attribs.end();

				const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
				attribs.emplace(it, Buffer::StrideView(buffers[bv.buffer], accessor.ByteStride(bv), bv.byteOffset, bv.byteLength),
					fmt, (uint32_t)accessor.byteOffset, vk::VertexInputRate::eVertex);
			}
			return geometry;
		});
		return geoms;
	});

	auto blank = make_shared<Texture>(device, "blank", vk::Extent3D(2, 2, 1), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	for (uint32_t i = 0; i < images.size(); i++)
		mGeometryPipeline->descriptor("gTextures", i) = sampled_texture_descriptor(images[i]);
	for (uint32_t i = (uint32_t)images.size(); i < mGeometryPipeline->descriptor_count("gTextures"); i++)
		mGeometryPipeline->descriptor("gTextures", i) = sampled_texture_descriptor(blank);
	mGeometryPipeline->descriptor("gMaterials") = commandBuffer.copy_buffer(materials, vk::BufferUsageFlagBits::eStorageBuffer);

	vector<Node*> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		const auto& node = model.nodes[n];
		Node& dst = mNode.make_child(node.name);
		nodes[n] = &dst;
		
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			auto transform = dst.make_component<TransformData>(float3::Zero(), 1.f, QUATF_I);
			if (!node.translation.empty()) transform->Translation = Map<const Array3d>(node.translation.data()).cast<float>();
			if (!node.rotation.empty()) 	 transform->Rotation = make_quatf((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			if (!node.scale.empty()) 			 transform->Scale = (float)Map<const Vector3d>(node.scale.data()).norm();
		}
		if (node.mesh < model.meshes.size()) {
			for (size_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++) {
				auto& prim = model.meshes[node.mesh].primitives[i];
				auto& indices = model.accessors[prim.indices];

				size_t indexStride = 0;
				switch (indices.componentType) {
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					indexStride = sizeof(uint8_t);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				case TINYGLTF_COMPONENT_TYPE_SHORT:
					indexStride = sizeof(uint16_t);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				case TINYGLTF_COMPONENT_TYPE_INT:
					indexStride = sizeof(uint32_t);
					break;
				}
				
				auto d = dst.make_child("Submesh").make_component<Submesh>();
				d->mGeometry = geometries[node.mesh][i];
				d->mIndices = Buffer::StrideView(buffers[model.bufferViews[indices.bufferView].buffer], indexStride, model.bufferViews[indices.bufferView].byteOffset, model.bufferViews[indices.bufferView].byteLength);
				d->mIndexCount = (uint32_t)indices.count;
				d->mFirstVertex = 0;
				d->mFirstIndex = (uint32_t)(indices.byteOffset/indexStride);
				d->mMaterialIndex = prim.material;
			}
		}

		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			auto light = dst.make_child(l.name).make_component<LightData>();
			light->mEmission = (Map<const Array3d>(l.color.data()) * l.intensity).cast<float>();
			if (l.type == "directional") {
				light->mType = LightType_Distant;
				light->mShadowProjection = make_orthographic(16, 16, 0, 0, 512, false);
			} else if (l.type == "point") {
				light->mType = LightType_Point;
				light->mShadowProjection = make_perspective(numbers::pi_v<float>/2, 1, 0, 0, 128, false);
			} else if (l.type == "spot") {
				light->mType = LightType_Spot;
				double co = cos(l.spot.outerConeAngle);
				light->mSpotAngleScale = (float)(1/(cos(l.spot.innerConeAngle) - co));
				light->mSpotAngleOffset = -(float)(co * light->mSpotAngleScale);
				light->mShadowProjection = make_perspective((float)l.spot.outerConeAngle, 1, 0, 0, 128, false);
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

void RasterScene::pre_render(CommandBuffer& commandBuffer, const shared_ptr<Framebuffer>& framebuffer, const component_ptr<Camera>& camera) const {
	ProfilerRegion s("RasterScene::pre_render", commandBuffer);

	buffer_vector<TransformData> transforms(commandBuffer.mDevice);
	buffer_vector<LightData> lights(commandBuffer.mDevice);
	
	Texture::View shadowMap;
	{
		ProfilerRegion s("make gShadowMap");
		shadowMap = make_shared<Texture>(commandBuffer.mDevice, "gShadowMap", vk::Extent3D(2048,2048,1), vk::Format::eD32Sfloat, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);
	}
	{
		ProfilerRegion s("Compute transforms");
		mNode.for_each_descendant<Submesh>([&](const component_ptr<Submesh>& d) {
			d->mInstanceIndex = (uint32_t)transforms.size();
			transforms.emplace_back(node_to_world(d.node()));
		});
		mNode.for_each_descendant<LightData>([&](const component_ptr<LightData>& l) {
			l->mLightToWorld = node_to_world(l.node());
			lights.emplace_back(*l);
		});
	}
	
	mGeometryPipeline->descriptor("gTransforms") = commandBuffer.copy_buffer(transforms, vk::BufferUsageFlagBits::eStorageBuffer);
	
	mShadowNode->render(commandBuffer, { { "gShadowMap", { shadowMap, vk::ClearValue({1.f, 0}) } } });

	mGeometryPipeline->descriptor("gShadowMap") = sampled_texture_descriptor(shadowMap);
	mGeometryPipeline->descriptor("gLights") = commandBuffer.copy_buffer(lights, vk::BufferUsageFlagBits::eStorageBuffer);
	mGeometryPipeline->push_constant("gLightCount", (uint32_t)lights.size());
	mGeometryPipeline->push_constant("gWorldToCamera", inverse(node_to_world(camera.node())));
	if (camera->mProjectionMode&ProjectionMode_Perspective)
		mGeometryPipeline->push_constant("gProjection", make_perspective(camera->mVerticalFoV, (float)framebuffer->extent().height/(float)framebuffer->extent().width, 0, 0, camera->mFar, camera->mProjectionMode&ProjectionMode_RightHanded));
	else
		mGeometryPipeline->push_constant("gProjection", make_orthographic(camera->mOrthographicHeight, (float)framebuffer->extent().height/(float)framebuffer->extent().width, 0, 0, camera->mFar, camera->mProjectionMode&ProjectionMode_RightHanded));
	
	mGeometryPipeline->transition_images(commandBuffer);
	mSkyboxPipeline->transition_images(commandBuffer);
	mGizmoPipeline->transition_images(commandBuffer);
}

void RasterScene::draw(CommandBuffer& commandBuffer, vk::ShaderStageFlags stageMask) const {
	ProfilerRegion s("RasterScene::draw", commandBuffer);

	unordered_map<shared_ptr<Geometry>, list<component_ptr<Submesh>>> drawables;
	mNode.for_each_descendant<Submesh>([&](const component_ptr<Submesh>& d) {
		drawables[d->mGeometry].push_back(d);
	});
	for (const auto&[geometry,ds] : drawables) {
		mGeometryPipeline->bind_pipeline(commandBuffer, *geometry, stageMask);
		mGeometryPipeline->bind_descriptor_sets(commandBuffer);
		mGeometryPipeline->push_constants(commandBuffer);
		for (const component_ptr<Submesh>& d : ds) {
			commandBuffer.push_constant("gMaterialIndex", d->mMaterialIndex);
			geometry->drawIndexed(commandBuffer, d->mIndices, d->mIndexCount, 1, d->mFirstIndex, d->mFirstVertex, d->mInstanceIndex);
		}
	}

	if (stageMask & vk::ShaderStageFlagBits::eFragment) {
		mSkyboxPipeline->bind_pipeline(commandBuffer, Geometry(vk::PrimitiveTopology::eTriangleList), stageMask);
		mSkyboxPipeline->bind_descriptor_sets(commandBuffer);
		mGeometryPipeline->push_constants(commandBuffer);
		mSkyboxPipeline->push_constants(commandBuffer);
		commandBuffer->draw(6, 1, 0, 0);

		vector<Gizmo*> gizmos;
		unordered_map<Texture::View, uint32_t> textures;
		mNode.for_each_descendant<Gizmo>([&](const component_ptr<Gizmo>& g) {
			uint32_t t = ~0;
			if (g->mTexture) {
				if (auto it = textures.find(g->mTexture); it != textures.end())
					t = it->second;
				else {
					t = (uint32_t)textures.size();
					textures.emplace(g->mTexture, t);
					mGizmoPipeline->descriptor("gTextures", t) = sampled_texture_descriptor(g->mTexture);
				}
			}
			gizmos.push_back(g.get());
		});
		if (!gizmos.empty()) {
			mGizmoPipeline->descriptor("gTextures", textures.size()) = sampled_texture_descriptor(make_shared<Texture>(commandBuffer.mDevice, "blank", vk::Extent3D(2, 2, 1), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled));
			textures.emplace(Texture::View(), (uint32_t)textures.size());

			Geometry geometry(vk::PrimitiveTopology::eTriangleList, {
				{ Geometry::AttributeType::ePosition, { Geometry::Attribute(commandBuffer.copy_buffer(mGizmoVertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::Format::eR32G32B32Sfloat,    offsetof(gizmo_vertex_t, mPosition)) } },
				{ Geometry::AttributeType::eColor,    { Geometry::Attribute(commandBuffer.copy_buffer(mGizmoVertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::Format::eR32G32B32A32Sfloat, offsetof(gizmo_vertex_t, mColor)) } },
				{ Geometry::AttributeType::eTexcoord, { Geometry::Attribute(commandBuffer.copy_buffer(mGizmoVertices, vk::BufferUsageFlagBits::eVertexBuffer), vk::Format::eR32G32Sfloat,       offsetof(gizmo_vertex_t, mTexcoord)) } }
			});
			auto inds = commandBuffer.copy_buffer(mGizmoIndices, vk::BufferUsageFlagBits::eIndexBuffer);
			mGizmoPipeline->bind_pipeline(commandBuffer, geometry, stageMask);
			mGizmoPipeline->bind_descriptor_sets(commandBuffer);
			mGizmoPipeline->push_constants(commandBuffer);
			for (Gizmo* g : gizmos) {
				commandBuffer.push_constant("gTextureIndex", textures.at(g->mTexture));
				commandBuffer.push_constant("gTextureST", g->mTextureST);
				commandBuffer.push_constant("gColor", g->mColor);
				geometry.drawIndexed(commandBuffer, inds, g->mIndexCount, 1, g->mFirstIndex, g->mFirstVertex, 0);
			}
		}
	}
}

void RasterScene::gizmo_cube(const hlsl::TransformData& transform, const hlsl::float4& color) {

}
void RasterScene::gizmo_sphere(const hlsl::TransformData& transform, const hlsl::float4& color) {

}
void RasterScene::gizmo_quad(const hlsl::TransformData& transform, const hlsl::float4& color, const Texture::View& texture, const hlsl::float4& textureST) {
	mGizmos.push_back({});
	mGizmoVertices.emplace_back({ transform_point(transform, float3(-1,-1,0)), color, float2(0,0) });
	mGizmoVertices.emplace_back({ transform_point(transform, float3( 1,-1,0)), color, float2(1,0) });
	mGizmoVertices.emplace_back({ transform_point(transform, float3(-1, 1,0)), color, float2(0,1) });
	mGizmoVertices.emplace_back({ transform_point(transform, float3( 1, 1,0)), color, float2(1,1) });
	mGizmoIndices.emplace_back(0);
	mGizmoIndices.emplace_back(1);
	mGizmoIndices.emplace_back(2);
	mGizmoIndices.emplace_back(1);
	mGizmoIndices.emplace_back(3);
	mGizmoIndices.emplace_back(2);
}