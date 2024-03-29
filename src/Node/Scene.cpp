#include "Scene.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

#include <portable-file-dialogs.h>

namespace stm {

static float3 gAnimateTranslate = float3::Zero();
static float3 gAnimateRotate = float3::Zero();
static float3 gAnimateWiggleBase = float3::Zero();
static float3 gAnimateWiggleOffset = float3::Zero();
static float gAnimateWiggleSpeed = 1;
static float gAnimateWiggleTime = 0;
static TransformData* gAnimatedTransform = nullptr;

static bool gChanged = false;

inline void inspector_gui_fn(Inspector& inspector, Scene* scene) { scene->on_inspector_gui(); }
inline void inspector_gui_fn(Inspector& inspector, Camera* cam) {
	bool ortho = cam->mProjection.orthographic();
	if (ImGui::Checkbox("Orthographic", reinterpret_cast<bool*>(&ortho)))
		cam->mProjection.vertical_fov = -cam->mProjection.vertical_fov;
	ImGui::DragFloat("Near Plane", &cam->mProjection.near_plane, 0.01f, -1, 1);
	if (cam->mProjection.orthographic()) {
		ImGui::DragFloat("Far Plane", &cam->mProjection.far_plane, 0.01f, -1, 1);
		ImGui::DragFloat2("Projection Scale", cam->mProjection.scale.data(), 0.01f, -1, 1);
	} else {
		float fovy = degrees(2 * atan(1 / cam->mProjection.scale[1]));
		if (ImGui::DragFloat("Vertical FoV", &fovy, 0.01f, 1, 179)) {
			const float aspect = cam->mProjection.scale[0] / cam->mProjection.scale[1];
			cam->mProjection.scale[1] = 1 / tan(radians(fovy / 2));
			cam->mProjection.scale[0] = cam->mProjection.scale[1] * aspect;
		}
	}
	ImGui::DragFloat2("Projection Offset", cam->mProjection.offset.data(), 0.01f, -1, 1);
	ImGui::InputInt2("ImageRect Offset", &cam->mImageRect.offset.x);
	ImGui::InputInt2("ImageRect Extent", reinterpret_cast<int32_t*>(&cam->mImageRect.extent.width));
}
inline void inspector_gui_fn(Inspector& inspector, TransformData* t) {
	TransformData prev = *t;

	float3 translate = t->m.topRightCorner(3, 1);
	float3 scale;
	scale.x() = t->m.block(0, 0, 3, 1).matrix().norm();
	scale.y() = t->m.block(0, 1, 3, 1).matrix().norm();
	scale.z() = t->m.block(0, 2, 3, 1).matrix().norm();
	const Eigen::Matrix3f r = t->m.block<3, 3>(0, 0).matrix() * Eigen::DiagonalMatrix<float, 3, 3>(1 / scale.x(), 1 / scale.y(), 1 / scale.z());
	Eigen::Quaternionf rotation(r);

	if (ImGui::DragFloat3("Translation", translate.data(), .1f) && !translate.hasNaN()) {
		t->m.topRightCorner(3, 1) = translate;
		gChanged = true;
	}

	bool v = ImGui::DragFloat3("Rotation (XYZ)", rotation.vec().data(), .1f);
	v |= ImGui::DragFloat("Rotation (W)", &rotation.w(), .1f);
	if (v && !rotation.vec().hasNaN()) {
		t->m.block<3, 3>(0, 0) = rotation.normalized().matrix();
		gChanged = true;
	}

	if (ImGui::DragFloat3("Scale", scale.data(), .1f) && !rotation.vec().hasNaN() && !scale.hasNaN()) {
		t->m.block<3, 3>(0, 0) = rotation.normalized().matrix() * Eigen::DiagonalMatrix<float, 3, 3>(scale.x() == 0 ? 1 : scale.x(), scale.y() == 0 ? 1 : scale.y(), scale.z() == 0 ? 1 : scale.z());
		gChanged = true;
	}

	if (gAnimatedTransform == t) {
		if (ImGui::Button("Stop Animating")) gAnimatedTransform = nullptr;
		ImGui::DragFloat3("Move", gAnimateTranslate.data(), .01f);
		ImGui::DragFloat3("Rotate", gAnimateRotate.data(), .01f);
		const bool was_zero = gAnimateWiggleOffset.isZero();
		ImGui::DragFloat3("Wiggle Offset", gAnimateWiggleOffset.data(), .01f);
		if (!gAnimateWiggleOffset.isZero()) {
			if (ImGui::Button("Set Wiggle Anchor") || was_zero) {
				gAnimateWiggleBase = translate;
				gAnimateWiggleTime = 0;
			}
			ImGui::DragFloat("Wiggle Speed", &gAnimateWiggleSpeed);
		}
	} else if (ImGui::Button("Animate"))
		gAnimatedTransform = t;
}
inline void inspector_gui_fn(Inspector& inspector, MeshPrimitive* mesh) {
	if (mesh->mMesh) {
		ImGui::Text("%s", type_index(typeid(Mesh)).name());
		ImGui::SameLine();
		inspector.component_ptr_field(mesh->mMesh);
	}
	if (mesh->mMaterial) {
		ImGui::Text("%s", type_index(typeid(Material)).name());
		ImGui::SameLine();
		inspector.component_ptr_field(mesh->mMaterial);
	}
}
inline void inspector_gui_fn(Inspector& inspector, SpherePrimitive* sphere) {
	if (ImGui::DragFloat("Radius", &sphere->mRadius, .01f)) gChanged = true;
	if (sphere->mMaterial) {
		ImGui::Text("%s", type_index(typeid(Material)).name());
		ImGui::SameLine();
		inspector.component_ptr_field(sphere->mMaterial);
	}
}
inline void inspector_gui_fn(Inspector& inspector, Environment* v) { v->inspector_gui(); }
inline void inspector_gui_fn(Inspector& inspector, Material* v) { v->inspector_gui(); }
inline void inspector_gui_fn(Inspector& inspector, Medium* v) { v->inspector_gui(); }

TransformData node_to_world(const Node& node) {
	TransformData transform = make_transform(float3::Zero(), quatf_identity(), float3::Ones());
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

void Scene::load_environment_map(Node& root, CommandBuffer& commandBuffer, const fs::path& filepath) {
	root.make_component<Environment>(load_environment(commandBuffer, filepath));
}

ImageValue1 Scene::alpha_to_roughness(CommandBuffer& commandBuffer, const ImageValue1& alpha) {
	ImageValue1 roughness;
	roughness.value = sqrt(alpha.value);
	if (alpha.image) {
		roughness.image = make_shared<Image>(commandBuffer.mDevice, "roughness", alpha.image.extent(), alpha.image.image()->format(), 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
		mConvertAlphaToRoughnessPipeline->descriptor("gInput")  = image_descriptor(alpha.image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertAlphaToRoughnessPipeline->descriptor("gRoughnessRW") = image_descriptor(roughness.image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mConvertAlphaToRoughnessPipeline->get_pipeline());
		mConvertAlphaToRoughnessPipeline->bind_descriptor_sets(commandBuffer);
		mConvertAlphaToRoughnessPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(alpha.image.extent());
		roughness.image.image()->generate_mip_maps(commandBuffer);
		cout << "Converted alpha to roughness: " << alpha.image.image()->name() << endl;
	}
	return roughness;
}
ImageValue1 Scene::shininess_to_roughness(CommandBuffer& commandBuffer, const ImageValue1& shininess) {
	ImageValue1 roughness;
	roughness.value = sqrt(2 / (shininess.value + 2));
	if (shininess.image) {
		roughness.image = make_shared<Image>(commandBuffer.mDevice, "roughness", shininess.image.extent(), shininess.image.image()->format(), 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
		mConvertShininessToRoughnessPipeline->descriptor("gInput")  = image_descriptor(shininess.image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertShininessToRoughnessPipeline->descriptor("gRoughnessRW") = image_descriptor(roughness.image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		commandBuffer.bind_pipeline(mConvertShininessToRoughnessPipeline->get_pipeline());
		mConvertShininessToRoughnessPipeline->bind_descriptor_sets(commandBuffer);
		mConvertShininessToRoughnessPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(shininess.image.extent());
		roughness.image.image()->generate_mip_maps(commandBuffer);
		cout << "Converted shininess to roughness: " << shininess.image.image()->name() << endl;
	}
	return roughness;
}

Material Scene::make_metallic_roughness_material(CommandBuffer& commandBuffer, const ImageValue3& base_color, const ImageValue4& metallic_roughness, const ImageValue3& transmission, const float eta, const ImageValue3& emission) {
	Material dst;
	if ((emission.value > 0).any()) {
		dst.values[0].image = emission.image;
		dst.base_color() = emission.value/luminance(emission.value);
		dst.emission() = luminance(emission.value);
		dst.eta() = 0; // eta
		return dst;
	}
	dst.base_color() = base_color.value;
	dst.emission() = 0;
	dst.metallic() = metallic_roughness.value.z(); // metallic
	dst.roughness() = metallic_roughness.value.y(); // roughness
	dst.anisotropic() = 0; // anisotropic
	dst.subsurface() = 0; // subsurface
	dst.clearcoat() = 0; // clearcoat
	dst.clearcoat_gloss() = 1; // clearcoat gloss
	dst.transmission() = luminance(transmission.value);
	dst.eta() = eta;
	if (base_color.image || metallic_roughness.image || transmission.image) {
		Image::View d = base_color.image ? base_color.image : metallic_roughness.image ? metallic_roughness.image : transmission.image;
		for (int i = 0; i < DISNEY_DATA_N; i++) {
			dst.values[i].image = make_shared<Image>(commandBuffer.mDevice, "material data", d.extent(), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
			mConvertPbrPipeline->descriptor("gOutput",i) = image_descriptor(dst.values[i].image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		}
		if (base_color.image)
			dst.alpha_mask = make_shared<Image>(commandBuffer.mDevice, "alpha mask", d.extent(), vk::Format::eR8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
		mConvertPbrPipeline->descriptor("gOutputAlphaMask") = image_descriptor(dst.alpha_mask ? dst.alpha_mask : dst.values[0].image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

		dst.min_alpha = make_shared<Buffer>(commandBuffer.mDevice, "min_alpha", sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
		dst.min_alpha[0] = 0xFFFFFFFF;
		mConvertPbrPipeline->descriptor("gOutputMinAlpha") = dst.min_alpha;

		mConvertPbrPipeline->descriptor("gDiffuse") = image_descriptor(base_color.image ? base_color.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertPbrPipeline->descriptor("gSpecular") = image_descriptor(metallic_roughness.image ? metallic_roughness.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertPbrPipeline->descriptor("gTransmittance") = image_descriptor(transmission.image ? transmission.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertPbrPipeline->specialization_constant<uint32_t>("gUseDiffuse") = (bool)base_color.image;
		mConvertPbrPipeline->specialization_constant<uint32_t>("gUseSpecular") = (bool)metallic_roughness.image;
		mConvertPbrPipeline->specialization_constant<uint32_t>("gUseTransmittance") = (bool)transmission.image;
		commandBuffer.bind_pipeline(mConvertPbrPipeline->get_pipeline());
		mConvertPbrPipeline->bind_descriptor_sets(commandBuffer);
		mConvertPbrPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(d.extent());
		for (int i = 0; i < DISNEY_DATA_N; i++)
			dst.values[i].image.image()->generate_mip_maps(commandBuffer);
	}
	return dst;
}
Material Scene::make_diffuse_specular_material(CommandBuffer& commandBuffer, const ImageValue3& diffuse, const ImageValue3& specular, const ImageValue1& roughness, const ImageValue3& transmission, const float eta, const ImageValue3& emission) {
	Material dst;
	if ((emission.value > 0).any()) {
		dst.values[0].image = emission.image;
		dst.base_color() = emission.value/luminance(emission.value);
		dst.emission() = luminance(emission.value);
		dst.eta()  = 0; // eta
		return dst;
	}
	const float ld = luminance(diffuse.value);
	const float ls = luminance(specular.value);
	const float lt = luminance(transmission.value);
	dst.base_color() = (diffuse.value*ld + specular.value*ls + transmission.value*lt) / (ld + ls + lt);
	dst.emission() = 0;
	dst.metallic() = ls / (ld + ls + lt);
	dst.roughness() = roughness.value;
	dst.anisotropic() = 0;
	dst.subsurface() = 0;
	dst.clearcoat() = 0;
	dst.clearcoat_gloss() = 1;
	dst.transmission() = lt / (ld + ls + lt);
	dst.eta() = eta;
	if (diffuse.image || specular.image || transmission.image || roughness.image) {
		Image::View d = diffuse.image ? diffuse.image : specular.image ? specular.image : transmission.image ? transmission.image : roughness.image;
		for (int i = 0; i < DISNEY_DATA_N; i++) {
			dst.values[i].image = make_shared<Image>(commandBuffer.mDevice, "material data", d.extent(), vk::Format::eR8G8B8A8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
			mConvertDiffuseSpecularPipeline->descriptor("gOutput",i) = image_descriptor(dst.values[i].image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);
		}
		if (diffuse.image)
			dst.alpha_mask = make_shared<Image>(commandBuffer.mDevice, "alpha mask", d.extent(), vk::Format::eR8Unorm, 1, 0, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage);
		mConvertDiffuseSpecularPipeline->descriptor("gOutputAlphaMask") = image_descriptor(dst.alpha_mask ? dst.alpha_mask : dst.values[0].image, vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite);

		dst.min_alpha = make_shared<Buffer>(commandBuffer.mDevice, "min_alpha", sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_ONLY);
		dst.min_alpha[0] = 0xFFFFFFFF;
		mConvertDiffuseSpecularPipeline->descriptor("gOutputMinAlpha") = dst.min_alpha;

		mConvertDiffuseSpecularPipeline->descriptor("gDiffuse") = image_descriptor(diffuse.image ? diffuse.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertDiffuseSpecularPipeline->descriptor("gSpecular") = image_descriptor(specular.image ? specular.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertDiffuseSpecularPipeline->descriptor("gTransmittance") = image_descriptor(transmission.image ? transmission.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertDiffuseSpecularPipeline->descriptor("gRoughness") = image_descriptor(roughness.image ? roughness.image : d, vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead);
		mConvertDiffuseSpecularPipeline->specialization_constant<uint32_t>("gUseDiffuse") = (bool)diffuse.image;
		mConvertDiffuseSpecularPipeline->specialization_constant<uint32_t>("gUseSpecular") = (bool)specular.image;
		mConvertDiffuseSpecularPipeline->specialization_constant<uint32_t>("gUseTransmittance") = (bool)transmission.image;
		mConvertDiffuseSpecularPipeline->specialization_constant<uint32_t>("gUseRoughness") = (bool)roughness.image;
		commandBuffer.bind_pipeline(mConvertDiffuseSpecularPipeline->get_pipeline());
		mConvertDiffuseSpecularPipeline->bind_descriptor_sets(commandBuffer);
		mConvertDiffuseSpecularPipeline->push_constants(commandBuffer);
		commandBuffer.dispatch_over(d.extent());
		for (int i = 0; i < DISNEY_DATA_N; i++)
			dst.values[i].image.image()->generate_mip_maps(commandBuffer);
	}
	return dst;
}

Scene::Scene(Node& node) : mNode(node) {
	auto app = mNode.find_in_ancestor<Application>();
	app.node().find_in_descendants<Inspector>()->register_inspector_gui_fn<Scene>(&inspector_gui_fn);
	app->OnUpdate.add_listener(mNode, bind_front(&Scene::update, this), Node::EventPriority::eDefault);

	component_ptr<Inspector> gui = node.node_graph().find_components<Inspector>().front();
	gui->register_inspector_gui_fn<TransformData>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<Camera>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<MeshPrimitive>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<SpherePrimitive>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<Environment>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<Material>(&inspector_gui_fn);
	gui->register_inspector_gui_fn<Medium>(&inspector_gui_fn);

	gAnimatedTransform = nullptr;

	mCopyVerticesPipeline 				 = make_shared<ComputePipelineState>("copy_vertices", make_shared<Shader>(app->window().mInstance.device(), "Shaders/copy_vertices.spv"));
	mConvertDiffuseSpecularPipeline 	 = make_shared<ComputePipelineState>("material_convert_from_diffuse_specular", make_shared<Shader>(app->window().mInstance.device(), "Shaders/material_convert_from_diffuse_specular.spv"));
	mConvertPbrPipeline 				 = make_shared<ComputePipelineState>("material_convert_from_gltf_pbr", make_shared<Shader>(app->window().mInstance.device(), "Shaders/material_convert_from_gltf_pbr.spv"));
	mConvertAlphaToRoughnessPipeline 	 = make_shared<ComputePipelineState>("material_convert_alpha_to_roughness", make_shared<Shader>(app->window().mInstance.device(), "Shaders/material_convert_alpha_to_roughness.spv"));
	mConvertShininessToRoughnessPipeline = make_shared<ComputePipelineState>("material_convert_shininess_to_roughness", make_shared<Shader>(app->window().mInstance.device(), "Shaders/material_convert_shininess_to_roughness.spv"));

	for (const string& arg : app->window().mInstance.find_arguments("scene"))
		mToLoad.emplace_back(arg);
}

void Scene::on_inspector_gui() {
	if (mSceneData) {
		ImGui::Text("%lu instances", mSceneData->mInstances.size());
		ImGui::Text("%lu light instances", mSceneData->mLightInstanceMap.size());
		ImGui::Text("%lu emissive primitives", mSceneData->mEmissivePrimitiveCount);
		ImGui::Text("%u materials", mSceneData->mMaterialCount);
	}
	if (ImGui::Button("Load File")) {
		auto f = pfd::open_file("Open scene", "", loader_filters());
		for (const string& filepath : f.result())
			mToLoad.emplace_back(filepath);
	}
	ImGui::Checkbox("Always Update", &mAlwaysUpdate);
}

void Scene::update(CommandBuffer& commandBuffer, const float deltaTime) {
	ProfilerRegion s("Scene::update", commandBuffer);

	if (gAnimatedTransform) {
		float r = length(gAnimateRotate);
		quatf rotate = (r > 0) ? angle_axis(r * deltaTime, gAnimateRotate / r) : quatf_identity();
		*gAnimatedTransform = tmul(*gAnimatedTransform, make_transform(gAnimateTranslate * deltaTime, rotate, float3::Ones()));
		if (!gAnimateWiggleOffset.isZero()) {
			gAnimatedTransform->m.topRightCorner(3, 1) = gAnimateWiggleBase + gAnimateWiggleOffset*sin(gAnimateWiggleTime);
			gAnimateWiggleTime += deltaTime*gAnimateWiggleSpeed;
		}
		mUpdateOnce = true;
	}

	if (gChanged) {
		mUpdateOnce = true;
		gChanged = false;
	}

	bool update = mAlwaysUpdate || mUpdateOnce;

	if (commandBuffer.mDevice.mInstance.window().input_state().pressed(KeyCode::eKeyControl) &&
		commandBuffer.mDevice.mInstance.window().pressed_redge(KeyCode::eKeyO)) {
		auto f = pfd::open_file("Open scene", "", loader_filters());
		for (const string& filepath : f.result())
			mToLoad.emplace_back(filepath);
	}
	for (const string& file : commandBuffer.mDevice.mInstance.window().input_state().files())
		mToLoad.emplace_back(file);

	bool loaded = false;
	for (const string& file : mToLoad) {
		const fs::path filepath = file;
		const string name = filepath.filename().string();
		Node& n = mNode.make_child(name);
		try {
			load(n, commandBuffer, filepath);
			loaded = true;
			update = true;
		} catch (exception e) {
			cout << "Failed to load " << filepath << ": " << e.what() << endl;
			mNode.node_graph().erase(n);
		}
	}
	mToLoad.clear();

	if (!update) return;

	mUpdateOnce = loaded && !mAlwaysUpdate;

	uint32_t totalVertexCount = 0;
	uint32_t totalIndexBufferSize = 0;

	auto mPrevFrame = mSceneData;
	mSceneData = make_shared<SceneData>();

	mSceneData->mResources = {};
	mSceneData->mInstanceTransformMap = {};
	mSceneData->mResources.distribution_data_size = 0;

	mSceneData->mEmissivePrimitiveCount = 0;
	mSceneData->mMaterialCount = 0;
	ByteAppendBuffer materialData;
	materialData.data.reserve(mPrevFrame && mPrevFrame->mMaterialData ? mPrevFrame->mMaterialData.size() / sizeof(uint32_t) : 1);
	unordered_map<const Material*, uint32_t> materialMap;

	vector<vk::AccelerationStructureInstanceKHR> instancesAS;
	vector<vk::BufferMemoryBarrier> blasBarriers;

	vector<tuple<MeshPrimitive*, MeshAS*, uint32_t>> meshInstanceIndices;
	vector<InstanceData> instanceDatas;
	vector<TransformData> instanceTransforms;
	vector<TransformData> instanceInverseTransforms;
	vector<TransformData> instanceMotionTransforms;
	if (mPrevFrame) {
		instanceDatas.reserve(mPrevFrame->mInstances.size());
		instanceTransforms.reserve(mPrevFrame->mInstances.size());
		instanceInverseTransforms.reserve(mPrevFrame->mInstances.size());
		instanceMotionTransforms.reserve(mPrevFrame->mInstances.size());
	}
	vector<uint32_t> lightInstanceMap;
	vector<float> lightInstancePowers;

	mSceneData->mInstanceNodes.clear();

	mSceneData->mInstanceIndexMap = make_shared<Buffer>(commandBuffer.mDevice, "InstanceIndexMap", sizeof(uint32_t) * max<size_t>(1, mPrevFrame ? mPrevFrame->mInstances.size() : 0), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
	ranges::fill(mSceneData->mInstanceIndexMap, -1);

	auto process_material = [&](const Material* material) {
		// append unique materials to materials list
		auto materialMap_it = materialMap.find(material);
		if (materialMap_it == materialMap.end()) {
			materialMap_it = materialMap.emplace(material, (uint32_t)(materialData.data.size() * sizeof(uint32_t))).first;
			material->store(materialData, mSceneData->mResources);
			mSceneData->mMaterialCount++;
		}
		return materialMap_it->second;
	};

	auto process_instance = [&](const component_ptr<void>& prim, const InstanceData& instance, const TransformData& transform, const float emissive_power) {
		const uint32_t instance_index = (uint32_t)instanceDatas.size();
		instanceDatas.emplace_back(instance);
		mSceneData->mInstanceNodes.emplace_back(&prim.node());

		uint32_t light_index = INVALID_INSTANCE;
		if (emissive_power > 0) {
			light_index = (uint32_t)lightInstanceMap.size();
			BF_SET(instanceDatas[instance_index].packed[1], lightInstancePowers.size(), 0, 12);
			lightInstanceMap.emplace_back(instance_index);
			lightInstancePowers.emplace_back(emissive_power);
		}

		// transforms

		TransformData prevTransform;
		if (mPrevFrame) {
			if (auto it = mPrevFrame->mInstanceTransformMap.find(prim.get()); it != mPrevFrame->mInstanceTransformMap.end()) {
				prevTransform = it->second.first;
				mSceneData->mInstanceIndexMap[it->second.second] = instance_index;
			}
		}
		mSceneData->mInstanceTransformMap.emplace(prim.get(), make_pair(transform, instance_index));

		const TransformData inv_transform = transform.inverse();
		instanceTransforms.emplace_back(transform);
		instanceInverseTransforms.emplace_back(inv_transform);
		instanceMotionTransforms.emplace_back(make_instance_motion_transform(inv_transform, prevTransform));
		return instance_index;
	};

	{ // mesh instances
		ProfilerRegion s("Process mesh instances", commandBuffer);
		mNode.for_each_descendant<MeshPrimitive>([&](const component_ptr<MeshPrimitive>& prim) {
			if (prim->mMesh->topology() != vk::PrimitiveTopology::eTriangleList) return;
			if (!prim->mMaterial) return;

			// build BLAS
			const size_t key = hash_args(prim->mMesh.get(), prim->mMaterial->alpha_test());
			auto it = mMeshAccelerationStructures.find(key);
			if (it == mMeshAccelerationStructures.end()) {
				ProfilerRegion s("build acceleration structures", commandBuffer);
				const auto& [vertexPosDesc, positions] = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];

				if (prim->mMesh->index_type() != vk::IndexType::eUint32 && prim->mMesh->index_type() != vk::IndexType::eUint16)
					return;

				vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
				triangles.vertexFormat = vertexPosDesc.mFormat;
				triangles.vertexData = commandBuffer.hold_resource(positions).device_address();
				triangles.vertexStride = vertexPosDesc.mStride;
				triangles.maxVertex = (uint32_t)(positions.size_bytes() / vertexPosDesc.mStride);
				triangles.indexType = prim->mMesh->index_type();
				triangles.indexData = commandBuffer.hold_resource(prim->mMesh->indices()).device_address();
				vk::AccelerationStructureGeometryKHR triangleGeometry(vk::GeometryTypeKHR::eTriangles, triangles, prim->mMaterial->alpha_test() ? vk::GeometryFlagBitsKHR{} : vk::GeometryFlagBitsKHR::eOpaque);
				vk::AccelerationStructureBuildRangeInfoKHR range(prim->mMesh->indices().size() / (prim->mMesh->indices().stride() * 3));
				auto as = make_shared<AccelerationStructure>(commandBuffer, prim.node().name() + "/BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, triangleGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());

				// copy vertex data
				if (mMeshVertices.find(prim->mMesh.get()) == mMeshVertices.end()) {
					Buffer::View<PackedVertexData>& vertices = mMeshVertices.emplace(prim->mMesh.get(),
						make_shared<Buffer>(commandBuffer.mDevice, prim.node().name() + "/PackedVertexData", triangles.maxVertex * sizeof(PackedVertexData), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress)).first->second;

					auto positions = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::ePosition)[0];
					auto normals = prim->mMesh->vertices()->at(VertexArrayObject::AttributeType::eNormal)[0];
					auto texcoords = prim->mMesh->vertices()->find(VertexArrayObject::AttributeType::eTexcoord);
					//auto tangents = prim->mMesh->vertices()->find(VertexArrayObject::AttributeType::eTangent);

					mCopyVerticesPipeline->descriptor("gVertices") = vertices;
					mCopyVerticesPipeline->descriptor("gPositions") = Buffer::View(positions.second, positions.first.mOffset);
					mCopyVerticesPipeline->descriptor("gNormals") = Buffer::View(normals.second, normals.first.mOffset);
					//mCopyVerticesPipeline->descriptor("gTangents") = tangents ? Buffer::View(tangents->second, tangents->first.mOffset) : positions.second;
					mCopyVerticesPipeline->descriptor("gTexcoords") = texcoords ? Buffer::View(texcoords->second, texcoords->first.mOffset) : positions.second;
					mCopyVerticesPipeline->push_constant<uint32_t>("gCount") = vertices.size();
					mCopyVerticesPipeline->push_constant<uint32_t>("gPositionStride") = positions.first.mStride;
					mCopyVerticesPipeline->push_constant<uint32_t>("gNormalStride") = normals.first.mStride;
					//mCopyVerticesPipeline->push_constant<uint32_t>("gTangentStride") = tangents ? tangents->first.mStride : 0;
					mCopyVerticesPipeline->push_constant<uint32_t>("gTexcoordStride") = texcoords ? texcoords->first.mStride : 0;
					commandBuffer.bind_pipeline(mCopyVerticesPipeline->get_pipeline());
					mCopyVerticesPipeline->bind_descriptor_sets(commandBuffer);
					mCopyVerticesPipeline->push_constants(commandBuffer);
					commandBuffer.dispatch_over(triangles.maxVertex);
					commandBuffer.barrier({ vertices }, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
				}

				it = mMeshAccelerationStructures.emplace(key, MeshAS{ as, prim->mMesh->indices() }).first;
			}

			const uint32_t material_address = process_material(prim->mMaterial.get());

			const uint32_t triCount = prim->mMesh->indices().size_bytes() / (prim->mMesh->indices().stride() * 3);
			const TransformData transform = node_to_world(prim.node());
			const float area = 1;

			if (prim->mMaterial->emission() > 0)
				mSceneData->mEmissivePrimitiveCount += triCount;

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Eigen::Matrix<float, 3, 4, Eigen::RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = process_instance(component_ptr<void>(prim), make_instance_triangles(material_address, triCount, totalVertexCount, totalIndexBufferSize, (uint32_t)it->second.mIndices.stride()), transform, prim->mMaterial->emission() * area);
			instance.mask = BVH_FLAG_TRIANGLES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(*commandBuffer.hold_resource(it->second.mAccelerationStructure));

			meshInstanceIndices.emplace_back(prim.get(), &it->second, (uint32_t)instance.instanceCustomIndex);
			totalVertexCount += mMeshVertices.at(prim->mMesh.get()).size();
			totalIndexBufferSize += align_up(it->second.mIndices.size_bytes(), 4);
		});
	}

	{ // sphere instances
		ProfilerRegion s("Process sphere instances", commandBuffer);
		mNode.for_each_descendant<SpherePrimitive>([&](const component_ptr<SpherePrimitive>& prim) {
			if (!prim->mMaterial) return;
			uint32_t material_address = process_material(prim->mMaterial.get());

			if (prim->mMaterial->emission() > 0)
				mSceneData->mEmissivePrimitiveCount++;

			TransformData transform = node_to_world(prim.node());
			const float r = prim->mRadius * transform.m.block<3, 3>(0, 0).matrix().determinant();
			transform = make_transform(transform.m.col(3).head<3>(), quatf_identity(), float3::Ones());

			const float3 mn = -float3::Constant(r);
			const float3 mx = float3::Constant(r);
			const size_t key = hash_args(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], prim->mMaterial->alpha_test());
			auto aabb_it = mAABBs.find(key);
			if (aabb_it == mAABBs.end()) {
				Buffer::View<vk::AabbPositionsKHR> aabb = make_shared<Buffer>(commandBuffer.mDevice, "aabb data", sizeof(vk::AabbPositionsKHR), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU);
				aabb[0].minX = mn[0];
				aabb[0].minY = mn[1];
				aabb[0].minZ = mn[2];
				aabb[0].maxX = mx[0];
				aabb[0].maxY = mx[1];
				aabb[0].maxZ = mx[2];
				vk::AccelerationStructureGeometryAabbsDataKHR aabbs(commandBuffer.hold_resource(aabb).device_address(), sizeof(vk::AabbPositionsKHR));
				vk::AccelerationStructureGeometryKHR aabbGeometry(vk::GeometryTypeKHR::eAabbs, aabbs, prim->mMaterial->alpha_test() ? vk::GeometryFlagBitsKHR{} : vk::GeometryFlagBitsKHR::eOpaque);
				vk::AccelerationStructureBuildRangeInfoKHR range(1);
				shared_ptr<AccelerationStructure> as = make_shared<AccelerationStructure>(commandBuffer, "aabb BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, aabbGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());
				aabb_it = mAABBs.emplace(key, as).first;
			}

			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Eigen::Matrix<float, 3, 4, Eigen::RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = process_instance(component_ptr<void>(prim), make_instance_sphere(material_address, r), transform, prim->mMaterial->emission() * (4*M_PI*r*r));
			instance.mask = BVH_FLAG_SPHERES;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);
		});
	}

	{ // media
		ProfilerRegion s("Process media", commandBuffer);
		mNode.for_each_descendant<Medium>([&](const component_ptr<Medium>& vol) {
			if (!vol) return;

			auto materialMap_it = materialMap.find(reinterpret_cast<Material*>(vol.get()));
			if (materialMap_it == materialMap.end()) {
				materialMap_it = materialMap.emplace(reinterpret_cast<Material*>(vol.get()), (uint32_t)(materialData.data.size() * sizeof(uint32_t))).first;
				mSceneData->mMaterialCount++;
				vol->store(materialData, mSceneData->mResources);
			}

			uint32_t material_address = materialMap_it->second;

			auto density_grid = vol->density_grid->grid<float>();
			const nanovdb::Vec3R& mn = density_grid->worldBBox().min();
			const nanovdb::Vec3R& mx = density_grid->worldBBox().max();
			const size_t key = hash_args((float)mn[0], (float)mn[1], (float)mn[2], (float)mx[0], (float)mx[1], (float)mx[2]);
			auto aabb_it = mAABBs.find(key);
			if (aabb_it == mAABBs.end()) {
				Buffer::View<vk::AabbPositionsKHR> aabb = make_shared<Buffer>(commandBuffer.mDevice, "aabb data", sizeof(vk::AabbPositionsKHR), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU);
				aabb[0].minX = (float)mn[0];
				aabb[0].minY = (float)mn[1];
				aabb[0].minZ = (float)mn[2];
				aabb[0].maxX = (float)mx[0];
				aabb[0].maxY = (float)mx[1];
				aabb[0].maxZ = (float)mx[2];
				vk::AccelerationStructureGeometryAabbsDataKHR aabbs(commandBuffer.hold_resource(aabb).device_address(), sizeof(vk::AabbPositionsKHR));
				vk::AccelerationStructureGeometryKHR aabbGeometry(vk::GeometryTypeKHR::eAabbs, aabbs, vk::GeometryFlagBitsKHR::eOpaque);
				vk::AccelerationStructureBuildRangeInfoKHR range(1);
				shared_ptr<AccelerationStructure> as = make_shared<AccelerationStructure>(commandBuffer, "aabb BLAS", vk::AccelerationStructureTypeKHR::eBottomLevel, aabbGeometry, range);
				blasBarriers.emplace_back(
					vk::AccessFlagBits::eAccelerationStructureWriteKHR, vk::AccessFlagBits::eAccelerationStructureReadKHR,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					**as->buffer().buffer(), as->buffer().offset(), as->buffer().size_bytes());
				aabb_it = mAABBs.emplace(key, as).first;
			}

			const TransformData transform = node_to_world(vol.node());
			vk::AccelerationStructureInstanceKHR& instance = instancesAS.emplace_back();
			Eigen::Matrix<float, 3, 4, Eigen::RowMajor>::Map(&instance.transform.matrix[0][0]) = to_float3x4(transform);
			instance.instanceCustomIndex = process_instance(component_ptr<void>(vol), make_instance_volume(material_address, mSceneData->mResources.volume_data_map.at(vol->density_buffer)), transform, false);
			instance.mask = BVH_FLAG_VOLUME;
			instance.accelerationStructureReference = commandBuffer.mDevice->getAccelerationStructureAddressKHR(**aabb_it->second);
		});
	}

	// light distribution
	if (lightInstancePowers.size() > 0) {
		Buffer::View<float> lightPdf = make_shared<Buffer>(commandBuffer.mDevice, "light_pdf", lightInstancePowers.size()*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		Buffer::View<float> lightCdf = make_shared<Buffer>(commandBuffer.mDevice, "light_cdf", (lightInstancePowers.size()+1)*sizeof(float), vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU);
		build_distribution(lightInstancePowers,
			span(lightPdf.data(), lightPdf.size()),
			span(lightCdf.data(), lightCdf.size()) );
		mSceneData->mLightDistributionPDF = mSceneData->mResources.get_index(lightPdf);
		mSceneData->mLightDistributionCDF = mSceneData->mResources.get_index(lightCdf);
	} else
		mSceneData->mLightDistributionPDF = mSceneData->mLightDistributionCDF = -1;

	{ // Build TLAS
		ProfilerRegion s("Build TLAS", commandBuffer);
		commandBuffer.barrier(blasBarriers, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR);
		vk::AccelerationStructureGeometryKHR geom{ vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryInstancesDataKHR() };
		vk::AccelerationStructureBuildRangeInfoKHR range{ (uint32_t)instancesAS.size() };
		if (!instancesAS.empty()) {
			auto buf = make_shared<Buffer>(commandBuffer.mDevice, "TLAS instance buffer", sizeof(vk::AccelerationStructureInstanceKHR) * instancesAS.size(), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
			memcpy(buf->data(), instancesAS.data(), buf->size());
			commandBuffer.hold_resource(buf);
			geom.geometry.instances.data = buf->device_address();
		}
		mSceneData->mScene = make_shared<AccelerationStructure>(commandBuffer, mNode.name() + "/TLAS", vk::AccelerationStructureTypeKHR::eTopLevel, geom, range);
		commandBuffer.barrier({ commandBuffer.hold_resource(mSceneData->mScene).buffer() },
			vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, vk::AccessFlagBits::eAccelerationStructureWriteKHR,
			vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eAccelerationStructureReadKHR);
	}

	{ // environment material
		ProfilerRegion s("Process environment", commandBuffer);
		mSceneData->mEnvironmentMaterialAddress = -1;
		mNode.for_each_descendant<Environment>([&](const component_ptr<Environment> environment) {
			if (environment && !environment->emission.value.isZero() && mSceneData->mEnvironmentMaterialAddress  == -1) {
				mSceneData->mEnvironmentMaterialAddress = (uint32_t)(materialData.data.size() * sizeof(uint32_t));
				mSceneData->mMaterialCount++;
				environment->store(materialData, mSceneData->mResources);
			}
		});
	}

	{ // copy vertices and indices
		ProfilerRegion s("Copy vertex data", commandBuffer);

		if (!mSceneData->mVertices || mSceneData->mVertices.size() < totalVertexCount)
			mSceneData->mVertices = make_shared<Buffer>(commandBuffer.mDevice, "gVertices", max(totalVertexCount, 1u) * sizeof(PackedVertexData), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);
		if (!mSceneData->mIndices || mSceneData->mIndices.size() < totalIndexBufferSize)
			mSceneData->mIndices = make_shared<Buffer>(commandBuffer.mDevice, "gIndices", align_up(max(totalIndexBufferSize, 1u), sizeof(uint32_t)), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 4);

		for (const auto& [prim, blas, instanceIndex] : meshInstanceIndices) {
			const InstanceData& d = instanceDatas[instanceIndex];
			Buffer::View<PackedVertexData>& meshVertices = mMeshVertices.at(prim->mMesh.get());
			commandBuffer.copy_buffer(meshVertices, Buffer::View<PackedVertexData>(mSceneData->mVertices.buffer(), d.first_vertex() * sizeof(PackedVertexData), meshVertices.size()));
			commandBuffer.copy_buffer(blas->mIndices, Buffer::View<std::byte>(mSceneData->mIndices.buffer(), d.indices_byte_offset(), blas->mIndices.size_bytes()));
		}
		commandBuffer.barrier({ mSceneData->mIndices, mSceneData->mVertices }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	}

	if (!mSceneData->mInstances || mSceneData->mInstances.size() < instanceDatas.size()) {
		mSceneData->mInstances = make_shared<Buffer>(commandBuffer.mDevice, "gInstances", max<size_t>(1, instanceDatas.size()) * sizeof(InstanceData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
		mSceneData->mInstanceTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gInstanceTransforms", max<size_t>(1, instanceDatas.size()) * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
		mSceneData->mInstanceInverseTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gInstanceInverseTransforms", max<size_t>(1, instanceDatas.size()) * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
		mSceneData->mInstanceMotionTransforms = make_shared<Buffer>(commandBuffer.mDevice, "gInstanceMotionTransforms", max<size_t>(1, instanceDatas.size()) * sizeof(TransformData), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	}
	if (!mSceneData->mMaterialData || mSceneData->mMaterialData.size_bytes() < materialData.data.size() * sizeof(uint32_t))
		mSceneData->mMaterialData = make_shared<Buffer>(commandBuffer.mDevice, "gMaterialData", max<size_t>(1, materialData.data.size()) * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mSceneData->mLightInstanceMap || mSceneData->mLightInstanceMap.size() < lightInstanceMap.size())
		mSceneData->mLightInstanceMap = make_shared<Buffer>(commandBuffer.mDevice, "gLightInstanceMap", max<size_t>(1, lightInstanceMap.size()) * sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, 16);
	if (!mSceneData->mDistributionData || mSceneData->mDistributionData.size() < mSceneData->mResources.distribution_data_size)
		mSceneData->mDistributionData = make_shared<Buffer>(commandBuffer.mDevice, "gDistributionData", max<size_t>(1, mSceneData->mResources.distribution_data_size) * sizeof(float), vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer, VMA_MEMORY_USAGE_GPU_ONLY, 16);

	memcpy(mSceneData->mInstances.data(), instanceDatas.data(), instanceDatas.size() * sizeof(InstanceData));
	memcpy(mSceneData->mInstanceTransforms.data(), instanceTransforms.data(), instanceTransforms.size() * sizeof(TransformData));
	memcpy(mSceneData->mInstanceInverseTransforms.data(), instanceInverseTransforms.data(), instanceInverseTransforms.size() * sizeof(TransformData));
	memcpy(mSceneData->mInstanceMotionTransforms.data(), instanceMotionTransforms.data(), instanceMotionTransforms.size() * sizeof(TransformData));
	memcpy(mSceneData->mMaterialData.data(), materialData.data.data(), materialData.data.size() * sizeof(uint32_t));
	if (lightInstanceMap.size()) memcpy(mSceneData->mLightInstanceMap.data(), lightInstanceMap.data(), lightInstanceMap.size() * sizeof(uint32_t));
	for (const auto& [buf, address] : mSceneData->mResources.distribution_data_map)
		commandBuffer.copy_buffer(buf, Buffer::View<float>(mSceneData->mDistributionData, address, buf.size()));

	commandBuffer.barrier({ mSceneData->mDistributionData }, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderRead);
	commandBuffer.hold_resource(mSceneData->mDistributionData);
}

}