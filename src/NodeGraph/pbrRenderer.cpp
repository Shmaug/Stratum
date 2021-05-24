#include "pbrRenderer.hpp"
#include "../Core/Window.hpp"

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm;

pbrRenderer::pbrRenderer(NodeGraph& nodeGraph, Device& device, const shared_ptr<SpirvModule>& vs, const shared_ptr<SpirvModule>& fs, vk::SampleCountFlagBits sampleCount, vk::ImageLayout dstLayout) : mNodeGraph(nodeGraph) {
	#pragma region render nodes
	vk::PipelineColorBlendAttachmentState blendOpaque;
	blendOpaque.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	mRenderNode = &nodeGraph.emplace("mainpass").emplace<RenderNode>();
	(*mRenderNode)[""].bind_point(vk::PipelineBindPoint::eGraphics);
	(*mRenderNode)[""]["primaryColor"] =  RenderPass::SubpassDescription::AttachmentInfo(
		RenderPass::AttachmentTypeFlags::eColor, blendOpaque, vk::AttachmentDescription({},
			device.mInstance.window().surface_format().format, sampleCount,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eColorAttachmentOptimal));
	(*mRenderNode)[""]["primaryDepth"] =  RenderPass::SubpassDescription::AttachmentInfo(
		RenderPass::AttachmentTypeFlags::eDepthStencil, blendOpaque, vk::AttachmentDescription({},
			vk::Format::eD32Sfloat, sampleCount,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal));
	(*mRenderNode)[""]["primaryResolve"] =  RenderPass::SubpassDescription::AttachmentInfo(
		RenderPass::AttachmentTypeFlags::eResolve, blendOpaque, vk::AttachmentDescription({},
			device.mInstance.window().surface_format().format, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eColorAttachmentOptimal, dstLayout));
			
	mShadowRenderNode = &nodeGraph.emplace("shadowpass").emplace<RenderNode>();
	mShadowRenderNode->node().set_parent(mRenderNode->node());
	(*mShadowRenderNode)[""].bind_point(vk::PipelineBindPoint::eGraphics);
	(*mShadowRenderNode)[""]["gShadowAtlas"] = RenderPass::SubpassDescription::AttachmentInfo(
		RenderPass::AttachmentTypeFlags::eDepthStencil, blendOpaque, vk::AttachmentDescription({},
			vk::Format::eD32Sfloat, vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal));
	#pragma endregion

	#pragma region materials
	mMaterial = make_shared<Material>("pbr", vs, fs);
	mShadowMaterial = make_shared<Material>("pbrShadow", vs);

	mMaterial->set_immutable_sampler("gSampler", make_shared<Sampler>(device, "gSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
		0, true, 8, false, vk::CompareOp::eAlways, 0, VK_LOD_CLAMP_NONE)));
	mMaterial->set_immutable_sampler("gShadowSampler", make_shared<Sampler>(device, "gShadowSampler", vk::SamplerCreateInfo({},
		vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder,
		0, true, 2, true, vk::CompareOp::eLess, 0, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite)));
	#pragma endregion

	auto render_fn = [&](Material& material, CommandBuffer& commandBuffer) {
		mNodeGraph.for_each_component<PrimitiveSet>([&](const PrimitiveSet& primitive) {
			material.bind(commandBuffer, primitive.mGeometry);
			material.bind_descriptor_sets(commandBuffer);
			material.push_constants(commandBuffer);
			commandBuffer.push_constant("MaterialIndex", primitive.mMaterialIndex);
			commandBuffer.push_constant("BaseColorTexture", primitive.mBaseColorTexture);
			commandBuffer.push_constant("NormalTexture", primitive.mNormalTexture);
			commandBuffer.push_constant("MetallicRoughnessTexture", primitive.mMetallicRoughnessTexture);
			commandBuffer.push_constant("OcclusionTexture", primitive.mOcclusionTexture);
			commandBuffer.push_constant("EmissionTexture", primitive.mEmissionTexture);
			primitive.mGeometry.drawIndexed(commandBuffer, primitive.mIndices, primitive.mIndexCount, 1, primitive.mFirstIndex, primitive.mVertexOffset, primitive.mMaterialIndex);
		});
	};
	mRenderNode->PreRender.emplace(mRenderNode->node(), bind_front(&pbrRenderer::pre_render, this));		
	mShadowRenderNode->OnRender.emplace(mRenderNode->node(), bind_front(render_fn, ref(*mShadowMaterial)) );
	mRenderNode->OnRender.emplace(mRenderNode->node(), bind_front(render_fn, ref(*mMaterial)) );		
}

void pbrRenderer::load_gltf(CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("pbrRenderer::load_gltf", commandBuffer);

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	string err;
	string warn;
	if (
		(filename.extension() == ".glb" && !loader.LoadBinaryFromFile(&model, &err, &warn, filename.string())) ||
		(filename.extension() == ".gltf" && !loader.LoadASCIIFromFile(&model, &err, &warn, filename.string())) )
		throw runtime_error(filename.string() + ": " + err);
	if (!warn.empty()) fprintf_color(ConsoleColor::eYellow, stderr, "%s: %s\n", filename.string().c_str(), warn.c_str());
	
	Device& device = commandBuffer.mDevice;

	vector<shared_ptr<Buffer>> buffers(model.buffers.size());
	buffer_vector<hlsl::MaterialData> materials(commandBuffer.mDevice, model.materials.size());
	vector<vector<Geometry>> geometries(model.meshes.size());
	vector<shared_ptr<Texture>> images(model.images.size());

	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<byte> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst);
		Buffer::View<byte> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", dst.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(tmp.data(), buffer.data.data(), tmp.size());
		commandBuffer.copy_buffer(tmp, dst);
		return dst.buffer_ptr();
	});
	ranges::transform(model.images, images.begin(), [&](const tinygltf::Image& image) {
		auto pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		commandBuffer.hold_resource(pixels);
		memcpy(pixels->data(), image.image.data(), pixels->size());
		
		vk::Format fmt = vk::Format::eUndefined;
		switch (image.pixel_type) {
			case TINYGLTF_COMPONENT_TYPE_BYTE:
				fmt = image.component == 1 ? vk::Format::eR8Snorm :
							image.component == 2 ? vk::Format::eR8G8Snorm :
							image.component == 3 ? vk::Format::eR8G8B8Snorm :
							image.component == 4 ? vk::Format::eR8G8B8A8Snorm : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				fmt = image.component == 1 ? vk::Format::eR8Unorm :
							image.component == 2 ? vk::Format::eR8G8Unorm :
							image.component == 3 ? vk::Format::eR8G8B8Unorm :
							image.component == 4 ? vk::Format::eR8G8B8A8Unorm : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_SHORT:
				fmt = image.component == 1 ? vk::Format::eR16Snorm :
							image.component == 2 ? vk::Format::eR16G16Snorm :
							image.component == 3 ? vk::Format::eR16G16B16Snorm :
							image.component == 4 ? vk::Format::eR16G16B16A16Snorm : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				fmt = image.component == 1 ? vk::Format::eR16Unorm :
							image.component == 2 ? vk::Format::eR16G16Unorm :
							image.component == 3 ? vk::Format::eR16G16B16Unorm :
							image.component == 4 ? vk::Format::eR16G16B16A16Unorm : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_INT:
				fmt = image.component == 1 ? vk::Format::eR32Sint :
							image.component == 2 ? vk::Format::eR32G32Sint :
							image.component == 3 ? vk::Format::eR32G32B32Sint :
							image.component == 4 ? vk::Format::eR32G32B32A32Sint : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				fmt = image.component == 1 ? vk::Format::eR32Uint :
							image.component == 2 ? vk::Format::eR32G32Uint :
							image.component == 3 ? vk::Format::eR32G32B32Uint :
							image.component == 4 ? vk::Format::eR32G32B32A32Uint : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_FLOAT:
				fmt = image.component == 1 ? vk::Format::eR32Sfloat :
							image.component == 2 ? vk::Format::eR32G32Sfloat :
							image.component == 3 ? vk::Format::eR32G32B32Sfloat :
							image.component == 4 ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eUndefined;
				break;
			case TINYGLTF_COMPONENT_TYPE_DOUBLE:
				fmt = image.component == 1 ? vk::Format::eR64Sfloat :
							image.component == 2 ? vk::Format::eR64G64Sfloat :
							image.component == 3 ? vk::Format::eR64G64B64Sfloat :
							image.component == 4 ? vk::Format::eR64G64B64A64Sfloat : vk::Format::eUndefined;
				break;
		}
		
		auto tex = make_shared<Texture>(device, image.name, vk::Extent3D(image.width, image.height, 1), fmt, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.hold_resource(tex);
		tex->transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
		commandBuffer->copyBufferToImage(**pixels, **tex, vk::ImageLayout::eTransferDstOptimal, { vk::BufferImageCopy(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, tex->array_layers()), {}, tex->extent()) });
		tex->generate_mip_maps(commandBuffer);
		return tex;
	});
	ranges::transform(model.materials, materials.begin(), [](const tinygltf::Material& material) {
		hlsl::MaterialData m;
		m.mEmission = Map<const Array3d>(material.emissiveFactor.data()).cast<float>();
		m.mBaseColor = Map<const Array4d>(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
		m.mMetallic = (float)material.pbrMetallicRoughness.metallicFactor;
		m.mRoughness = (float)material.pbrMetallicRoughness.roughnessFactor;
		m.mNormalScale = (float)material.normalTexture.scale;
		m.mOcclusionScale = (float)material.occlusionTexture.strength;
		return m;
	});
	ranges::transform(model.meshes, geometries.begin(), [&](const tinygltf::Mesh& mesh) {
		vector<Geometry> geoms(mesh.primitives.size());
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
			Geometry geometry(topo);

			for (const auto&[attribName,attribIndex] : prim.attributes) {
				const auto& accessor = model.accessors[attribIndex];

				vk::Format fmt = vk::Format::eUndefined;
				switch (accessor.componentType) {
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: 	
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR8Uint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR8G8Uint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR8G8B8Uint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR8G8B8A8Uint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_BYTE: 						
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR8Sint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR8G8Sint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR8G8B8Sint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR8G8B8A8Sint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: 	
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR16Uint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR16G16Uint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR16G16B16Uint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR16G16B16A16Uint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_SHORT: 					
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR16Sint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR16G16Sint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR16G16B16Sint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR16G16B16A16Sint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: 		
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR32Uint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR32G32Uint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR32G32Uint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR32G32Uint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_INT: 						
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR32Sint : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR32G32Sint :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR32G32Sint :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR32G32Sint : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_FLOAT: 					
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR32Sfloat : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR32G32Sfloat :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR32G32B32Sfloat :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eUndefined;
						break;
					case TINYGLTF_COMPONENT_TYPE_DOUBLE: 					
						fmt = accessor.type == TINYGLTF_TYPE_SCALAR ? vk::Format::eR64Sfloat : 
									accessor.type == TINYGLTF_TYPE_VEC2   ? vk::Format::eR64G64Sfloat :
									accessor.type == TINYGLTF_TYPE_VEC3   ? vk::Format::eR64G64B64Sfloat :
									accessor.type == TINYGLTF_TYPE_VEC4   ? vk::Format::eR64G64B64A64Sfloat : vk::Format::eUndefined;
						break;
				}
				
				// parse typename & typeindex
				uint32_t typeIdx = 0;
				string typeName;
				typeName.resize(attribName.size());
				ranges::transform(attribName.begin(), attribName.end(), typeName.begin(), [&](char c) { return tolower(c); });
				size_t c = typeName.find_first_of("0123456789");
				if (c != string::npos) {
					typeIdx = stoi(typeName.substr(c));
					typeName = typeName.substr(0, c);
				}
				if (typeName.back() == '_') typeName = typeName.substr(0, typeName.length() - 1);
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
				
				geometry[semanticMap.at(typeName)][typeIdx] = Geometry::Attribute(
					buffers[model.bufferViews[accessor.bufferView].buffer], fmt,
					(uint32_t)accessor.byteOffset, vk::VertexInputRate::eVertex, model.bufferViews[accessor.bufferView].byteOffset);
			}
			return geometry;
		});
		return geoms;
	});

	auto blank = make_shared<Texture>(device, "blank", vk::Extent3D(2, 2, 1), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
	for (uint32_t i = 0; i < images.size(); i++)
		mMaterial->descriptor("gTextures", i) = sampled_texture_descriptor(images[i]);
	for (uint32_t i = (uint32_t)images.size(); i < mMaterial->descriptor_count("gTextures"); i++)
		mMaterial->descriptor("gTextures", i) = sampled_texture_descriptor(blank);
	
	mMaterial->descriptor("gMaterials") = commandBuffer.copy_buffer(materials, vk::BufferUsageFlagBits::eStorageBuffer);

	mMaterial->cull_mode(vk::CullModeFlagBits::eFront);

	queue<pair<NodeGraph::Node*, int>> todo;
	for (const auto& s : model.scenes)
		for (auto& n : s.nodes)
			todo.push({ nullptr, n });
	while (!todo.empty()) {
		const auto& [parent, nodeIndex] = todo.front();
		const auto& node = model.nodes[nodeIndex];

		NodeGraph::Node& dst = node_graph().emplace(node.name);
		if (parent) dst.set_parent(*parent);
		for (int n : node.children) todo.push({ &dst, n });

		hlsl::TransformData& transform = dst.emplace<hlsl::TransformData>(hlsl::float3::Zero(), 1.f, hlsl::quatf::Identity());
		if (!node.translation.empty()) transform.Translation = Map<const Array3d>(node.translation.data()).cast<float>();
		if (!node.rotation.empty()) 	 transform.Rotation = Quaterniond(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]).cast<float>();
		if (!node.scale.empty()) 			 transform.Scale = (float)Map<const Vector3d>(node.scale.data()).norm();
		
		if (node.mesh < model.meshes.size()) {
			for (size_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++) {
				auto& prim = model.meshes[node.mesh].primitives[i];
				auto& geometry = geometries[node.mesh][i];
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
				
				PrimitiveSet& p = dst.emplace<PrimitiveSet>();
				p.mGeometry = geometry;
				p.mIndices = Buffer::StrideView(buffers[model.bufferViews[indices.bufferView].buffer], indexStride, model.bufferViews[indices.bufferView].byteOffset, model.bufferViews[indices.bufferView].byteLength);
				p.mIndexCount = (uint32_t)indices.count;
				p.mVertexOffset = 0;
				p.mFirstIndex = (uint32_t)(indices.byteOffset/indexStride);
				p.mMaterialIndex = prim.material;
				p.mBaseColorTexture = model.materials[prim.material].pbrMetallicRoughness.baseColorTexture.index;
				p.mNormalTexture = model.materials[prim.material].normalTexture.index;
				p.mMetallicRoughnessTexture = model.materials[prim.material].pbrMetallicRoughness.metallicRoughnessTexture.index;
				p.mOcclusionTexture = model.materials[prim.material].occlusionTexture.index;
				p.mEmissionTexture = model.materials[prim.material].emissiveTexture.index;
			}
		}

		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			hlsl::LightData& light = dst.emplace<hlsl::LightData>();
			light.mEmission = Map<const Array3d>(l.color.data()).cast<float>() * (float)l.intensity;
			light.mFlags = LIGHT_USE_SHADOWMAP;
			if (l.type == "directional") {
				light.mFlags |= LIGHT_ATTEN_DIRECTIONAL;
				light.mShadowProjection = hlsl::make_orthographic(3, 3, -512, 128);
			} else if (l.type == "point") {
				light.mFlags |= LIGHT_ATTEN_DISTANCE;
				light.mShadowProjection = hlsl::make_perspective(numbers::pi_v<float>/2, 1, 0, 128);
			} else if (l.type == "spot") {
				light.mFlags |= LIGHT_ATTEN_DISTANCE | LIGHT_ATTEN_ANGULAR;
				double co = cos(l.spot.outerConeAngle);
				light.mSpotAngleScale = (float)(1/(cos(l.spot.innerConeAngle) - co));
				light.mSpotAngleOffset = -(float)(co * light.mSpotAngleScale);
				light.mShadowProjection = hlsl::make_perspective((float)l.spot.outerConeAngle, 1, 0, 128);
			}
			light.mShadowBias = .00001f;
			light.mShadowST = Vector4f(1,1,0,0);
		}
 
		todo.pop();
	}
}

void pbrRenderer::pre_render(CommandBuffer& commandBuffer) const {
	ProfilerRegion s("pbrRenderer::pre_render", commandBuffer);

	Buffer::View<hlsl::MaterialData> materialBuffer = get<Buffer::StrideView>(mMaterial->descriptor("gMaterials"));

	buffer_vector<hlsl::TransformData> transforms(commandBuffer.mDevice, materialBuffer.size());
	buffer_vector<hlsl::LightData> lights(commandBuffer.mDevice);
	Texture::View shadowAtlas = make_shared<Texture>(commandBuffer.mDevice, "gShadowAtlas", vk::Extent3D(2048,2048,1), get<vk::AttachmentDescription>((*mShadowRenderNode)[""]["gShadowAtlas"]), vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled);

	for (auto& n : mNodeGraph.find_nodes<PrimitiveSet>()) {
		hlsl::TransformData transform(hlsl::float3::Zero(), 1.f, hlsl::quatf::Identity());
		n.for_each_ancestor<hlsl::TransformData>([&](const hlsl::TransformData& t) {
			transform = hlsl::tmul(transform, t);
		});
		for (const PrimitiveSet& p : n.components<PrimitiveSet>())
			transforms[p.mMaterialIndex] = transform;
	}

	if (transforms.empty()) return;
	
	for (auto& n : mNodeGraph.find_nodes<hlsl::LightData>()) {
		hlsl::TransformData transform(hlsl::float3::Zero(), 1.f, hlsl::quatf::Identity());
		n.for_each_ancestor<hlsl::TransformData>([&](const hlsl::TransformData& t) {
			transform = hlsl::tmul(transform, t);
		});
		for (const hlsl::LightData& l : n.components<hlsl::LightData>())
			lights.emplace_back(l).mLightToWorld = transform; // TODO: assign mShadowST
	}

	mMaterial->descriptor("gTransforms") = commandBuffer.copy_buffer(transforms, vk::BufferUsageFlagBits::eStorageBuffer);
	mMaterial->descriptor("gLights") = commandBuffer.copy_buffer(lights, vk::BufferUsageFlagBits::eStorageBuffer);
	mMaterial->descriptor("gShadowAtlas") = sampled_texture_descriptor(shadowAtlas);
	mMaterial->push_constant("LightCount", (uint32_t)lights.size());

	mShadowMaterial->descriptor("gTransforms") = mMaterial->descriptor("gTransforms");

	for (const hlsl::LightData& light : lights) {		
		mShadowMaterial->push_constant("WorldToCamera", inverse(light.mLightToWorld));
		mShadowMaterial->push_constant("Projection", light.mShadowProjection);
		// TODO: use light.mShadowST to set viewport
		mShadowRenderNode->render(commandBuffer, { { "gShadowAtlas", shadowAtlas } });
	}

	mMaterial->transition_images(commandBuffer);
}