#include "../Scene.hpp"

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm::hlsl;

namespace stm {

void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
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
	vector<Image::View> images(model.images.size());
	vector<component_ptr<Material>> materials(model.materials.size());
	vector<vector<component_ptr<Mesh>>> meshes(model.meshes.size());

	auto get_image = [&](uint32_t index, bool srgb) -> Image::View {
		if (index >= images.size()) return {};
		if (images[index]) return images[index];

		const tinygltf::Image& image = model.images[index];
		Buffer::View<unsigned char> pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		ranges::uninitialized_copy(image.image, pixels);

		vk::Format fmt;
		if (srgb) {
			static const std::array<vk::Format,4> formatMap { vk::Format::eR8Srgb, vk::Format::eR8G8Srgb, vk::Format::eR8G8B8Srgb, vk::Format::eR8G8B8A8Srgb };
			fmt = formatMap.at(image.component - 1);
		} else {
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
			fmt = formatMap.at(image.pixel_type).at(image.component - 1);
		}

		commandBuffer.barrier({pixels}, vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		auto img = make_shared<Image>(device, image.name, vk::Extent3D(image.width, image.height, 1), fmt, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.copy_buffer_to_image(pixels, Image::View(img, 0, 1));
		img->generate_mip_maps(commandBuffer);
		images[index] = img;
		return img;
	};

	vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eTransferSrc;
	#ifdef VK_KHR_buffer_device_address
	bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
	bufferUsage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
	#endif
	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<unsigned char> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", buffer.data.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		ranges::copy(buffer.data, tmp.begin());
		Buffer::View<unsigned char> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), bufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		commandBuffer.copy_buffer(tmp, dst);
		commandBuffer.barrier({dst}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eVertexInput, vk::AccessFlagBits::eVertexAttributeRead|vk::AccessFlagBits::eIndexRead);
		commandBuffer.barrier({dst}, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		return dst.buffer();
	});
	Node& materialsNode = root.make_child("materials");
	ranges::transform(model.materials, materials.begin(), [&](const tinygltf::Material& material) {
		component_ptr<Material> m = materialsNode.make_child(material.name).make_component<Material>();
		const float3 emissive = Array3d::Map(material.emissiveFactor.data()).cast<float>();
		if (emissive.any()) {
			Emissive e;
			e.emission.value = emissive;
			e.emission.image = get_image(material.emissiveTexture.index, true);
			*m = e;
		} else {
			if (material.pbrMetallicRoughness.metallicFactor > 0) {
				RoughPlastic r;
				r.diffuse_reflectance.value = Array3d::Map(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
				r.diffuse_reflectance.image = get_image(material.pbrMetallicRoughness.baseColorTexture.index, true);
				r.specular_reflectance.value = r.diffuse_reflectance.value * material.pbrMetallicRoughness.metallicFactor;
				r.specular_reflectance.image = r.diffuse_reflectance.image;
				r.roughness.value = (float)material.pbrMetallicRoughness.roughnessFactor;
				*m = r;
			} else {
				if (material.extensions.contains("KHR_materials_transmission")) {			
					RoughDielectric r;
					r.specular_reflectance.value = Array3d::Map(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
					r.specular_reflectance.image = get_image(material.pbrMetallicRoughness.baseColorTexture.index, true);
					r.specular_transmittance.value = r.specular_reflectance.value * material.extensions.at("KHR_materials_transmission").Get("transmissionFactor").GetNumberAsDouble();
					r.specular_transmittance.image = r.specular_reflectance.image;
					r.roughness.value = (float)material.pbrMetallicRoughness.roughnessFactor;
					r.eta = material.extensions.contains("KHR_materials_ior") ? (float)material.extensions.at("KHR_materials_ior").Get("ior").GetNumberAsDouble() : 1.5f;
					*m = r;
				} else {
					Lambertian l;
					l.reflectance.value = Array3d::Map(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
					l.reflectance.image = get_image(material.pbrMetallicRoughness.baseColorTexture.index, true);
					*m = l;
				}
			}
		}
		/*
		m->mMetallic = (float)material.pbrMetallicRoughness.metallicFactor;
		m->mRoughness = (float)material.pbrMetallicRoughness.roughnessFactor;
		m->mNormalScale = (float)material.normalTexture.scale;
		m->mOcclusionScale = (float)material.occlusionTexture.strength;
		if (material.pbrMetallicRoughness.baseColorTexture.index != -1) m->mAlbedoImage = images[model.textures[material.pbrMetallicRoughness.baseColorTexture.index].source];
		if (material.normalTexture.index != -1) m->mNormalImage = images[model.textures[material.normalTexture.index].source];
		if (material.emissiveTexture.index != -1) m->mEmissionImage = images[model.textures[material.emissiveTexture.index].source];
		if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1) m->mMetallicImage = m->mRoughnessImage = images[model.textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index].source];
		if (material.occlusionTexture.index != -1) m->mOcclusionImage = images[model.textures[material.occlusionTexture.index].source];
		m->mMetallicImageComponent = 0;
		m->mRoughnessImageComponent = 1;
		m->mOcclusionImageComponent = 0;

		if (const auto& it = material.extensions.find("KHR_materials_ior"); it != material.extensions.end())
			m->mIndexOfRefraction = (float)it->second.Get("ior").Get<double>();
		else
			m->mIndexOfRefraction = 1.5f;

		if (const auto& it = material.extensions.find("KHR_materials_transmission"); it != material.extensions.end())
			m->mTransmission = (float)it->second.Get("transmissionFactor").Get<double>();
		else
			m->mTransmission = 0;

		if (const auto& it = material.extensions.find("KHR_materials_volume"); it != material.extensions.end()) {
			const auto& c = it->second.Get("attenuationColor");
			m->mAbsorption = (-Array3d(c.Get(0).Get<double>(), c.Get(1).Get<double>(), c.Get(2).Get<double>()) / it->second.Get("attenuationDistance").Get<double>()).cast<float>();
		}

		*/
		if (material.alphaMode == "MASK")
			m.node().make_child("alpha_mask").make_component<Image::View>( get_image(material.pbrMetallicRoughness.baseColorTexture.index, true) );
		return m;
	});
	Node& meshesNode = root.make_child("meshes");
	for (uint32_t i = 0; i < model.meshes.size(); i++) {
		meshes[i].resize(model.meshes[i].primitives.size());
		for (uint32_t j = 0; j < model.meshes[i].primitives.size(); j++) {
			const tinygltf::Primitive& prim = model.meshes[i].primitives[j];
			const auto& indicesAccessor = model.accessors[prim.indices];
			const auto& indexBufferView = model.bufferViews[indicesAccessor.bufferView];
			const size_t stride = tinygltf::GetComponentSizeInBytes(indicesAccessor.componentType);
			const Buffer::StrideView indexBuffer = Buffer::StrideView(buffers[indexBufferView.buffer], stride, indexBufferView.byteOffset + indicesAccessor.byteOffset, indicesAccessor.count * stride);

			shared_ptr<VertexArrayObject> vertexData = make_shared<VertexArrayObject>();
			
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

				VertexArrayObject::AttributeType attributeType;
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
					static const unordered_map<string, VertexArrayObject::AttributeType> semanticMap {
						{ "position", 	VertexArrayObject::AttributeType::ePosition },
						{ "normal", 		VertexArrayObject::AttributeType::eNormal },
						{ "tangent", 		VertexArrayObject::AttributeType::eTangent },
						{ "bitangent", 	VertexArrayObject::AttributeType::eBinormal },
						{ "texcoord", 	VertexArrayObject::AttributeType::eTexcoord },
						{ "color", 			VertexArrayObject::AttributeType::eColor },
						{ "psize", 			VertexArrayObject::AttributeType::ePointSize },
						{ "pointsize", 	VertexArrayObject::AttributeType::ePointSize },
						{ "joints",     VertexArrayObject::AttributeType::eBlendIndex },
						{ "weights",    VertexArrayObject::AttributeType::eBlendWeight }
					};
					attributeType = semanticMap.at(typeName);
				}
				
				auto& attribs = (*vertexData)[attributeType];
				if (attribs.size() <= typeIndex) attribs.resize(typeIndex+1);
				const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
				uint32_t stride = accessor.ByteStride(bv);
				attribs[typeIndex] = {
					VertexArrayObject::AttributeDescription(stride, attributeFormat, 0, vk::VertexInputRate::eVertex),
					Buffer::View<byte>(buffers[bv.buffer], bv.byteOffset + accessor.byteOffset, stride*accessor.count) };
			}

			meshes[i][j] = meshesNode.make_child(model.meshes[i].name + "_" + to_string(j)).make_component<Mesh>(vertexData, indexBuffer, topology);
		}
	}

	vector<Node*> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		const auto& node = model.nodes[n];
		Node& dst = root.make_child(node.name);
		nodes[n] = &dst;
		
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			float3 translate;
			float3 scale;
			if (node.translation.empty()) translate = float3::Zero();
			else translate = Array3d::Map(node.translation.data()).cast<float>();
			if (node.scale.empty()) scale = float3::Ones();
			else scale = Array3d::Map(node.scale.data()).cast<float>();
			const quatf rotate = node.rotation.empty() ? quatf_identity() : qnormalize(make_quatf((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]));
			dst.make_component<TransformData>(make_transform(translate, rotate, scale));
		} else if (!node.matrix.empty())
			dst.make_component<TransformData>(from_float3x4(Array<double,4,4>::Map(node.matrix.data()).block<3,4>(0,0).cast<float>()));

		if (node.mesh < model.meshes.size())
			for (uint32_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++) {
				const auto& prim = model.meshes[node.mesh].primitives[i];
				dst.make_child(model.meshes[node.mesh].name).make_component<MeshPrimitive>(materials[prim.material], meshes[node.mesh][i]);
			}
		
		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			if (l.type == "point" && l.extras.Has("radius")) {
				auto sphere = dst.make_child(l.name).make_component<SpherePrimitive>();
				sphere->mRadius = (float)l.extras.Get("radius").GetNumberAsDouble();
				Emissive emissive;
				emissive.emission.image = {};
				emissive.emission.value = (Array3d::Map(l.color.data()) * l.intensity).cast<float>();
				sphere->mMaterial = materialsNode.make_child(l.name).make_component<Material>(emissive);
			}
		}
	}

	for (size_t i = 0; i < model.nodes.size(); i++)
		for (int c : model.nodes[i].children)
			nodes[c]->set_parent(*nodes[i]);

	cout << "Loaded " << filename << endl;
}

}