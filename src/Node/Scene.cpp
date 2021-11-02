#include "RasterScene.hpp"
#include "Application.hpp"
#include "Gui.hpp"

#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

using namespace stm;
using namespace stm::hlsl;

namespace stm {

static hlsl::float3 gAnimateTranslate = float3::Zero();
static hlsl::float3 gAnimateRotate = float3::Zero();
static hlsl::TransformData* gAnimatedTransform = nullptr;
STRATUM_API void animate(CommandBuffer& commandBuffer, float deltaTime) {
	if (gAnimatedTransform) {
		gAnimatedTransform->mTranslation += rotate_vector(gAnimatedTransform->mRotation, gAnimateTranslate);
		float r = length(gAnimateRotate);
		if (r > 0)
			gAnimatedTransform->mRotation = normalize(qmul(gAnimatedTransform->mRotation, angle_axis(r, gAnimateRotate/r)));
	}
}

inline void inspector_gui_fn(Camera* cam) {
	bool orthographic = cam->mProjectionMode == Camera::ProjectionMode::eOrthographic;
  if (ImGui::Checkbox("Orthographic", &orthographic))
		cam->mProjectionMode = orthographic ? Camera::ProjectionMode::eOrthographic : Camera::ProjectionMode::ePerspective;
  ImGui::DragFloat("Near Plane", &cam->mNear, 0.01f, -1, 1);
  ImGui::DragFloat("Far Plane", &cam->mFar, 0.1f, -1024, 1024);
  if (orthographic)
    ImGui::DragFloat("Vertical Size", &cam->mOrthographicHeight, .01f);
  else
    ImGui::DragFloat("Vertical FoV", &cam->mVerticalFoV, .01f, 0.00390625, numbers::pi_v<float>);
}
inline void inspector_gui_fn(TransformData* t) {
  ImGui::DragFloat3("Translation", t->mTranslation.data(), .1f);
  ImGui::DragFloat("Scale", &t->mScale, .05f);
  if (ImGui::DragFloat4("Rotation (XYZW)", t->mRotation.xyz.data(), .1f, -1, 1))
    t->mRotation = normalize(t->mRotation);
	
	if (gAnimatedTransform == t) {
			if (ImGui::Button("Stop Animating")) gAnimatedTransform = nullptr;
			ImGui::DragFloat3("Translate", gAnimateTranslate.data(), .01f);
			ImGui::DragFloat3("Rotate", gAnimateRotate.data(), .01f);
	} else if (ImGui::Button("Animate"))
		gAnimatedTransform = t;
}
inline void inspector_gui_fn(LightData* light) {
  if (ImGui::BeginListBox("Light to world")) {
    inspector_gui_fn(&light->mLightToWorld);
    ImGui::EndListBox();
  }

  ImGui::ColorEdit3("Emission", light->mEmission.data());
  const char* items[] { "Distant", "Point", "Spot" };
  if (ImGui::BeginCombo("Type", items[light->mType])) {
    for (uint32_t i = 0; i < ranges::size(items); i++)
      if (ImGui::Selectable(items[i], light->mType == i))
        light->mType = i;
    ImGui::EndCombo();
  }

	PackedLightData p { light->mPackedData };

	if (light->mType == LIGHT_TYPE_DISTANT || light->mType == LIGHT_TYPE_POINT || light->mType == LIGHT_TYPE_SPOT) {
		float r = p.radius();
    if (ImGui::DragFloat("Radius", &r, 0.01f, 0, 1)) p.radius(r);
		if (light->mType == LIGHT_TYPE_SPOT) {
			r = acosf( p.cos_inner_angle() );
    	if (ImGui::DragFloat("Inner Angle", &r, 0.01f, 0, M_PI)) p.cos_inner_angle(cosf(r));
			r = acosf( p.cos_outer_angle() );
    	if (ImGui::DragFloat("Outer Angle", &r, 0.01f, 0, M_PI)) p.cos_outer_angle(cosf(r));
		}
	}

	bool shadowmap = p.shadow_index() != -1;
  if (ImGui::Checkbox("Shadow map", &shadowmap)) p.shadow_index(shadowmap ? 0 : -1);
  if (shadowmap) {
    ImGui::DragFloat("Shadow Near Plane", &light->mShadowProjection.mNear, 0.01f, -1, 1);
    ImGui::DragFloat("Shadow Far Plane", &light->mShadowProjection.mFar, 0.1f, -1024, 1024);
		float shadowBias = p.shadow_bias();
  	if (ImGui::DragFloat("Shadow bias", &shadowBias, .1f, 0, 4)) p.shadow_bias(shadowBias);
	}

	light->mPackedData = p.v;
}
inline void inspector_gui_fn(EnvironmentMap* environment) {
	uint32_t w = ImGui::GetWindowSize().x;
	if (environment->mImage) {
		ImGui::Text("Base Color");
		if (environment->mImage) {
			ImGui::Text("Environment Map");
			ImGui::Image(&environment->mImage, ImVec2(w,w*(float)environment->mImage.extent().height/(float)environment->mImage.extent().width));
		}
		if (environment->mConditionalDistribution) {
			ImGui::Text("Conditional Distribution");
			ImGui::Image(&environment->mConditionalDistribution, ImVec2(w,16));
		}
		if (environment->mMarginalDistribution) {
			ImGui::Text("Marginal Distribution");
			ImGui::Image(&environment->mConditionalDistribution, ImVec2(w,w));
		}
	}
  ImGui::DragFloat("Gamma", &environment->mGamma, .01f);
}
inline void inspector_gui_fn(MaterialInfo* material) {
  ImGui::ColorEdit3("Albedo", material->mAlbedo.data());
  ImGui::ColorEdit3("Emission", material->mEmission.data(), ImGuiColorEditFlags_HDR);
  ImGui::ColorEdit3("mAbsorption", material->mAbsorption.data(), ImGuiColorEditFlags_HDR);
	ImGui::SliderFloat("Metallic", &material->mMetallic, 0, 1);
  ImGui::SliderFloat("Roughness", &material->mRoughness, 0, 1);
	ImGui::SliderFloat("Transmission", &material->mTransmission, 0, 1);
	ImGui::SliderFloat("IOR", &material->mIndexOfRefraction, 1, 3);
	ImGui::SliderFloat("Normal Scale", &material->mNormalScale, 0, 2);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("normalize((<sampled normal image value> * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))");
	ImGui::SliderFloat("Occlusion Scale", &material->mOcclusionScale, 0, 1);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("lerp(color, color * <sampled occlusion image value>, <occlusion strength>)");
	uint32_t w = ImGui::GetWindowSize().x;
	if (material->mAlbedoImage) {
		ImGui::Text("Albedo");
		ImGui::Image(&material->mAlbedoImage, ImVec2(w,w*(float)material->mAlbedoImage.extent().height/(float)material->mAlbedoImage.extent().width));
	}
	if (material->mNormalImage) {
		ImGui::Text("Normal");
		ImGui::Image(&material->mNormalImage, ImVec2(w,w*(float)material->mNormalImage.extent().height/(float)material->mNormalImage.extent().width));
	}
	if (material->mEmissionImage) {
		ImGui::Text("Emission Color");
		ImGui::Image(&material->mEmissionImage, ImVec2(w,w*(float)material->mEmissionImage.extent().height/(float)material->mEmissionImage.extent().width));
	}
	if (material->mMetallicImage) {
  	ImGui::SliderInt("Metallic Channel", reinterpret_cast<int*>(&material->mMetallicImageComponent), 0, 3);
		ImVec4 ch(0,0,0,1);
		*(&ch.x + material->mMetallicImageComponent%3) = 1;
		ImGui::Image(&material->mMetallicImage, ImVec2(w,w*(float)material->mMetallicImage.extent().height/(float)material->mMetallicImage.extent().width), ImVec2(0,0), ImVec2(1,1), ch);
	}
	if (material->mRoughnessImage) {
  	ImGui::SliderInt("Roughness Channel", reinterpret_cast<int*>(&material->mRoughnessImageComponent), 0, 3);
		ImVec4 ch(0,0,0,1);
		*(&ch.x + material->mRoughnessImageComponent%3) = 1;
		ImGui::Image(&material->mRoughnessImage, ImVec2(w,w*(float)material->mRoughnessImage.extent().height/(float)material->mRoughnessImage.extent().width), ImVec2(0,0), ImVec2(1,1), ch);
	}
	if (material->mOcclusionImage) {
  	ImGui::SliderInt("Occlusion Channel", reinterpret_cast<int*>(&material->mOcclusionImageComponent), 0, 3);
		ImVec4 ch(0,0,0,1);
		*(&ch.x + material->mOcclusionImageComponent%3) = 1;
		ImGui::Image(&material->mOcclusionImage, ImVec2(w,w*(float)material->mOcclusionImage.extent().height/(float)material->mOcclusionImage.extent().width), ImVec2(0,0), ImVec2(1,1), ch);
	}
}

TransformData node_to_world(const Node& node) {
	TransformData transform(float3(0,0,0), 1.f, make_quatf(0,0,0,1));
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

void EnvironmentMap::build_distributions(const span<float4>& img, const vk::Extent2D& extent, span<float2> marginalDistData, span<float2> conditionalDistData) {
	vector<float> pdf2D(extent.width*extent.height);
	vector<float> cdf2D(extent.width*extent.height);

	vector<float> pdf1D(extent.height);
	vector<float> cdf1D(extent.height);

	float colWeightSum = 0;

	for (uint32_t j = 0; j < extent.height; j++) {
		float rowWeightSum = 0;
		for (uint32_t i = 0; i < extent.width; ++i) {
			float weight = img[j*extent.width + i].matrix().dot(Vector4f(0.3f, 0.6f, 0.1f, 0));
			rowWeightSum += weight;
			pdf2D[j*extent.width + i] = weight;
			cdf2D[j*extent.width + i] = rowWeightSum;
		}
		float invRowSum = 1/rowWeightSum;
		for (uint32_t i = 0; i < extent.width; i++) {
			pdf2D[j*extent.width + i] *= invRowSum;
			cdf2D[j*extent.width + i] *= invRowSum;
		}
		colWeightSum += rowWeightSum;
		pdf1D[j] = rowWeightSum;
		cdf1D[j] = colWeightSum;
	}

	float invColSum = 1/colWeightSum;
	for (int j = 0; j < extent.height; j++) {
		cdf1D[j] *= invColSum;
		pdf1D[j] *= invColSum;
	}

	auto LowerBound = [](const vector<float>& array, uint32_t lower, uint32_t upper, float value) {
		while (lower < upper) {
			int mid = lower + (upper - lower) / 2;
			if (array[mid] < value)
				lower = mid + 1;
			else
				upper = mid;
		}
		return lower;
	};

	for (uint32_t i = 0; i < extent.height; i++) {
		uint32_t row = LowerBound(cdf1D, 0, extent.height, (float)(i + 1) / extent.height);
		marginalDistData[i].x() = row / (float)extent.height;
		marginalDistData[i].y() = pdf1D[i];
	}
	for (uint32_t j = 0; j < extent.height; j++)
		for (uint32_t i = 0; i < extent.width; i++) {
			uint32_t col = LowerBound(cdf2D, j*extent.width, (j + 1)*extent.width, (float)(i + 1) / extent.width) - j * extent.width;
			conditionalDistData[j*extent.width + i].x() = col / (float)extent.width;
			conditionalDistData[j*extent.width + i].y() = pdf2D[j*extent.width + i];
		}
}

void load_gltf(Node& root, CommandBuffer& commandBuffer, const fs::path& filename) {
	ProfilerRegion ps("pbrRenderer::load_gltf", commandBuffer);
	
	static bool registered = false;
	if (!registered) {
		registered = true;

		component_ptr<Gui> gui = root.node_graph().find_components<Gui>().front();
		gui->register_inspector_gui_fn<TransformData>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<LightData>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<Camera>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<EnvironmentMap>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<MaterialInfo>(&inspector_gui_fn);
		
		gAnimatedTransform = nullptr;
		root.node_graph().find_components<Application>().front()->OnUpdate.listen(gui.node(), &animate);
	}

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
	vector<component_ptr<MaterialInfo>> materials(model.materials.size());
	vector<vector<component_ptr<Mesh>>> meshes(model.meshes.size());

	vk::BufferUsageFlags bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst|vk::BufferUsageFlagBits::eTransferSrc;
	#ifdef VK_KHR_buffer_device_address
	bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
	#endif
	ranges::transform(model.buffers, buffers.begin(), [&](const tinygltf::Buffer& buffer) {
		Buffer::View<unsigned char> tmp = make_shared<Buffer>(device, buffer.name+"/Staging", buffer.data.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		ranges::copy(buffer.data, tmp.begin());
		Buffer::View<unsigned char> dst = make_shared<Buffer>(device, buffer.name, buffer.data.size(), bufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		commandBuffer.copy_buffer(tmp, dst);
		commandBuffer.barrier(dst, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eVertexInput, vk::AccessFlagBits::eVertexAttributeRead|vk::AccessFlagBits::eIndexRead);
		commandBuffer.barrier(dst, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
		return dst.buffer();
	});
	ranges::transform(model.images, images.begin(), [&](const tinygltf::Image& image) {
		Buffer::View<byte> pixels = make_shared<Buffer>(device, image.name+"/Staging", image.image.size(), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
		memcpy(pixels.data(), image.image.data(), pixels.size_bytes());
		
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
		
		commandBuffer.barrier(pixels, vk::PipelineStageFlagBits::eHost, vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
		auto img = make_shared<Image>(device, image.name, vk::Extent3D(image.width, image.height, 1), fmt, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
		commandBuffer.copy_buffer_to_image(pixels, Image::View(img, 0, 1));
		img->generate_mip_maps(commandBuffer);
		return img;
	});
	ranges::transform(model.materials, materials.begin(), [&](const tinygltf::Material& material) {
		auto m = root.make_child(material.name).make_component<MaterialInfo>();
		m->mEmission = Array3d::Map(material.emissiveFactor.data()).cast<float>();
		m->mAlbedo = Array3d::Map(material.pbrMetallicRoughness.baseColorFactor.data()).cast<float>();
		m->mMetallic = (float)material.pbrMetallicRoughness.metallicFactor;
		m->mRoughness = (float)material.pbrMetallicRoughness.roughnessFactor;
		m->mNormalScale = (float)material.normalTexture.scale;
		m->mOcclusionScale = (float)material.occlusionTexture.strength;
		if (material.pbrMetallicRoughness.baseColorTexture.index != -1)
			m->mAlbedoImage = images[material.pbrMetallicRoughness.baseColorTexture.index];
		else
			m->mAlbedoImage = {};
		if (material.normalTexture.index != -1)
			m->mNormalImage = images[material.normalTexture.index];
		else
			m->mNormalImage = {};
		if (material.emissiveTexture.index != -1)
			m->mEmissionImage = images[material.emissiveTexture.index];
		else
			m->mEmissionImage = {};
		if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
			m->mMetallicImage = m->mRoughnessImage = images[material.pbrMetallicRoughness.metallicRoughnessTexture.index];
		else
			m->mMetallicImage = m->mRoughnessImage = {};
		if (material.occlusionTexture.index != -1)
			m->mOcclusionImage = images[material.occlusionTexture.index];
		else
			m->mOcclusionImage = {};
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

		return m;
	});
	for (uint32_t i = 0; i < model.meshes.size(); i++) {
		meshes[i].resize(model.meshes[i].primitives.size());
		for (uint32_t j = 0; j < model.meshes[i].primitives.size(); j++) {
			const tinygltf::Primitive& prim = model.meshes[i].primitives[j];
			const auto& indicesAccessor = model.accessors[prim.indices];
			const auto& indexBufferView = model.bufferViews[indicesAccessor.bufferView];
			Buffer::StrideView indexBuffer = Buffer::StrideView(buffers[indexBufferView.buffer], tinygltf::GetComponentSizeInBytes(indicesAccessor.componentType), indexBufferView.byteOffset, indexBufferView.byteLength);

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
				attribs[typeIndex] = {
					VertexArrayObject::AttributeDescription(accessor.ByteStride(bv), attributeFormat, (uint32_t)accessor.byteOffset, vk::VertexInputRate::eVertex),
					Buffer::View<byte>(buffers[bv.buffer], bv.byteOffset, bv.byteLength) };
			}

			if (topology == vk::PrimitiveTopology::eTriangleList && !vertexData->find(VertexArrayObject::AttributeType::eTangent)) {
				Buffer::View<float4> tangents = make_shared<Buffer>(commandBuffer.mDevice, "tangents", sizeof(float4), bufferUsage, VMA_MEMORY_USAGE_GPU_ONLY);
				commandBuffer->fillBuffer(**tangents.buffer(), tangents.offset(), tangents.size_bytes(), 0);
				if (!vertexData->find(VertexArrayObject::AttributeType::eTexcoord)) {
					(*vertexData)[VertexArrayObject::AttributeType::eTexcoord].emplace_back(VertexArrayObject::AttributeDescription(sizeof(float2), vk::Format::eR32G32Sfloat, 0, vk::VertexInputRate::eVertex), tangents);
				} else {
					auto positions = (*vertexData)[VertexArrayObject::AttributeType::ePosition][0].second;
					auto texcoords = (*vertexData)[VertexArrayObject::AttributeType::eTexcoord][0].second;
					for (uint32_t i = 0; i < indicesAccessor.count; i++) {
						uint32_t i0 = *reinterpret_cast<uint32_t*>(indexBuffer.data() + indexBuffer.stride()*i);
						uint32_t i1 = *reinterpret_cast<uint32_t*>(indexBuffer.data() + indexBuffer.stride()*(i+1));
						uint32_t i2 = *reinterpret_cast<uint32_t*>(indexBuffer.data() + indexBuffer.stride()*(i+2));
						if (indexBuffer.stride() < sizeof(uint32_t)) {
							uint32_t mask = (1 << (indexBuffer.stride()*8)) - 1;
							i0 &= mask;
							i1 &= mask;
							i2 &= mask;
						}
						
						
					}
				}
				(*vertexData)[VertexArrayObject::AttributeType::eTangent].emplace_back(VertexArrayObject::AttributeDescription(sizeof(float4), vk::Format::eR32G32B32A32Sfloat, 0, vk::VertexInputRate::eVertex), tangents);
			}

			meshes[i][j] = root.make_child(model.meshes[i].name + "_" + to_string(j)).make_component<Mesh>(vertexData, indexBuffer, topology);
		}
	}

	vector<Node*> nodes(model.nodes.size());
	for (size_t n = 0; n < model.nodes.size(); n++) {
		const auto& node = model.nodes[n];
		Node& dst = root.make_child(node.name);
		nodes[n] = &dst;
		
		if (!node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
			auto transform = dst.make_component<TransformData>(float3::Zero(), 1.f, make_quatf(0,0,0,1));
			if (!node.translation.empty()) transform->mTranslation = Array3d::Map(node.translation.data()).cast<float>();
			if (!node.rotation.empty()) 	 transform->mRotation = make_quatf((float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2], (float)node.rotation[3]);
			if (!node.scale.empty()) 			 transform->mScale = (float)Vector3d::Map(node.scale.data()).norm();
		}

		if (node.mesh < model.meshes.size())
			for (uint32_t i = 0; i < model.meshes[node.mesh].primitives.size(); i++) {
				const auto& prim = model.meshes[node.mesh].primitives[i];
				const auto& indicesAccessor = model.accessors[prim.indices];
				dst.make_child(model.meshes[node.mesh].name).make_component<MeshPrimitive>(materials[prim.material], meshes[node.mesh][i], (uint32_t)(indicesAccessor.byteOffset/meshes[node.mesh][i]->indices().stride()), indicesAccessor.count);
			}
		
		auto light_it = node.extensions.find("KHR_lights_punctual");
		if (light_it != node.extensions.end() && light_it->second.Has("light")) {
			const tinygltf::Light& l = model.lights[light_it->second.Get("light").GetNumberAsInt()];
			auto light = dst.make_child(l.name).make_component<LightData>();
			PackedLightData p { light->mPackedData };
			light->mEmission = (Array3d::Map(l.color.data()) * l.intensity).cast<float>();
			p.radius(l.extras.Has("radius") ? (float)l.extras.Get("radius").GetNumberAsDouble() : .01f);
			if (l.type == "directional") {
				light->mType = LIGHT_TYPE_DISTANT;
				light->mShadowProjection = make_orthographic(float2(16, 16), float2::Zero(), -.0125f, -512.f);
			} else if (l.type == "point") {
				light->mType = LIGHT_TYPE_POINT;
				light->mShadowProjection = make_perspective(numbers::pi_v<float>/2, 1, float2::Zero(), -.0125f, -512.f);
			} else if (l.type == "spot") {
				light->mType = LIGHT_TYPE_SPOT;
				p.cos_inner_angle((float)cos(l.spot.innerConeAngle));
				p.cos_outer_angle((float)cos(l.spot.outerConeAngle));
				light->mShadowProjection = make_perspective((float)l.spot.outerConeAngle, 1, float2::Zero(), -.0125f, -512.f);
			}
			p.shadow_bias(.0001f);
			p.shadow_index(0);
			light->mPackedData = p.v;
		}

		if (node.camera != -1) {
			const tinygltf::Camera& cam = model.cameras[node.camera];
			if (cam.type == "perspective")
				dst.make_child(cam.name).make_component<Camera>(Camera::ProjectionMode::ePerspective , -(float)cam.perspective.znear, (float)cam.perspective.zfar, (float)cam.perspective.yfov);
			else if (cam.type == "orthographic")
				dst.make_child(cam.name).make_component<Camera>(Camera::ProjectionMode::eOrthographic, -(float)cam.orthographic.znear, (float)cam.orthographic.zfar, (float)cam.orthographic.ymag);
		}
	}

	for (size_t i = 0; i < model.nodes.size(); i++)
		for (int c : model.nodes[i].children)
			nodes[c]->set_parent(*nodes[i]);

	cout << "Loaded " << filename << endl;
}

}

/*
void DynamicGeometry::pre_render(CommandBuffer& commandBuffer) {
	if (mInstances.empty()) return;

	mPipeline->specialization_constant("gimageCount") = (uint32_t)mimages.size();
	for (const auto&[tex, idx] : mimages)
		mPipeline->descriptor("gimages", idx) = sampled_image_descriptor(tex);
	mPipeline->transition_images(commandBuffer);

	if (!mDrawData.first) {
		mDrawData.first = make_shared<VertexArrayObject>(VertexArrayObject::AttributeType::ePosition, VertexArrayObject::AttributeType::eTexcoord, VertexArrayObject::AttributeType::eColor);
		(*mDrawData.first)[VertexArrayObject::AttributeType::ePosition][0].first = VertexArrayObject::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, mPosition), vk::VertexInputRate::eVertex);
		(*mDrawData.first)[VertexArrayObject::AttributeType::eColor   ][0].first = VertexArrayObject::AttributeDescription(sizeof(vertex_t), vk::Format::eR8G8B8A8Unorm,   offsetof(vertex_t, mColor   ), vk::VertexInputRate::eVertex);
		(*mDrawData.first)[VertexArrayObject::AttributeType::eTexcoord][0].first = VertexArrayObject::AttributeDescription(sizeof(vertex_t), vk::Format::eR32G32Sfloat,    offsetof(vertex_t, mTexcoord), vk::VertexInputRate::eVertex);
	}
	(*mDrawData.first)[VertexArrayObject::AttributeType::ePosition][0].second = mVertices.buffer_view();
	(*mDrawData.first)[VertexArrayObject::AttributeType::eTexcoord][0].second = mVertices.buffer_view();
	(*mDrawData.first)[VertexArrayObject::AttributeType::eColor   ][0].second = mVertices.buffer_view();
	mDrawData.second = mIndices.buffer_view();
}
void DynamicGeometry::draw(CommandBuffer& commandBuffer, const TransformData& worldToCamera, const ProjectionData& projection) const {
	commandBuffer->setLineWidth(1.5f);
	mPipeline->push_constant<ProjectionData>("gProjection") = projection;
	for (const Instance& i : mInstances) {
		mPipeline->push_constant<TransformData>("gWorldToCamera") = tmul(worldToCamera, i.mTransform);
		mPipeline->push_constant<float4>("gColor") = i.mColor;
		mPipeline->push_constant<float4>("gimageST") = i.mimageST;
		mPipeline->push_constant<uint32_t>("gimageIndex") = i.mimageIndex;
		
		Mesh mesh(mDrawData.first, mDrawData.second, i.mTopology);
		commandBuffer.bind_pipeline(mPipeline->get_pipeline(commandBuffer.bound_framebuffer()->render_pass(), commandBuffer.subpass_index(), mesh.vertex_layout(*mPipeline->stage(vk::ShaderStageFlagBits::eVertex))));
		mPipeline->bind_descriptor_sets(commandBuffer);
		mPipeline->push_constants(commandBuffer);
		mesh.bind(commandBuffer);
		commandBuffer->drawIndexed(i.mIndexCount, 1, i.mFirstIndex, i.mFirstVertex, 0);
	}
}
*/