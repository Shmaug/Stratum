#include "Application.hpp"
#include "Scene.hpp"

using namespace stm::hlsl;

namespace stm {

static float3 gAnimateTranslate = float3::Zero();
static float3 gAnimateRotate = float3::Zero();
static TransformData* gAnimatedTransform = nullptr;
STRATUM_API void animate(CommandBuffer& commandBuffer, float deltaTime) {
	if (gAnimatedTransform) {
		*gAnimatedTransform = tmul(*gAnimatedTransform, make_transform(gAnimateTranslate*deltaTime, quatf_identity(), float3::Ones()));
		float r = length(gAnimateRotate);
		if (r > 0)
			*gAnimatedTransform = tmul(*gAnimatedTransform, make_transform(float3::Zero(), angle_axis(r*deltaTime, gAnimateRotate/r), float3::Ones()));
	}
}

inline void inspector_gui_fn(Camera* cam) {
  ImGui::DragFloat("Near Plane", &cam->mProjection.mNear, 0.01f, -1, 1);
	if (cam->mProjection.mOrthographic) {
  	ImGui::DragFloat("Far Plane", &cam->mProjection.mFar, 0.01f, -1, 1);

		ImGui::DragFloat2("Scale", cam->mProjection.mScale.data(), 0.01f, -1, 1);
	} else {
		float fovy = degrees(2*atan(1/cam->mProjection.mScale[1]));
		if (ImGui::DragFloat("Vertical FoV", &fovy, 0.01f, 1, 179)) {
			const float aspect = cam->mProjection.mScale[0]/cam->mProjection.mScale[1];
			cam->mProjection.mScale[1] = 1/tan(radians(fovy/2));
			cam->mProjection.mScale[0] = cam->mProjection.mScale[1]*aspect;
		}
	}
	ImGui::DragFloat2("Projection Offset", cam->mProjection.mOffset.data(), 0.01f, -1, 1);
  ImGui::Checkbox("Orthographic", reinterpret_cast<bool*>(&cam->mProjection.mOrthographic));
  ImGui::InputInt2("ImageRect Offset", &cam->mImageRect.offset.x);
  ImGui::InputInt2("ImageRect Extent", reinterpret_cast<int32_t*>(&cam->mImageRect.extent.width));
}
inline void inspector_gui_fn(TransformData* t) {
	TransformData prev = *t;

	#ifdef TRANSFORM_UNIFORM_SCALING
  ImGui::DragFloat3("Translation", t->mTranslation.data(), .1f);
  ImGui::DragFloat("Scale", &t->mScale, .05f);
  if (ImGui::DragFloat4("Rotation (XYZW)", t->mRotation.xyz.data(), .1f, -1, 1))
    t->mRotation = normalize(t->mRotation);
	#else
	float3 translate = t->m.topRightCorner(3,1);
  if (ImGui::DragFloat3("Translation", translate.data(), .1f)) t->m.topRightCorner(3,1) = translate;

	float3 scale;
	scale.x() = t->m.block(0, 0, 3, 1).matrix().norm();
	scale.y() = t->m.block(0, 1, 3, 1).matrix().norm();
	scale.z() = t->m.block(0, 2, 3, 1).matrix().norm();
	Matrix3f r = t->m.block<3,3>(0,0).matrix() * DiagonalMatrix<float,3,3>(1/scale.x(), 1/scale.y(), 1/scale.z());
	Quaternionf rotation(r);
	bool v = ImGui::DragFloat3("Rotation (XYZ)", rotation.vec().data(), .1f);
	v |= ImGui::DragFloat("Rotation (W)", &rotation.w(), .1f);
	v |= ImGui::DragFloat3("Scale", scale.data(), .1f);
	if (v) t->m.block<3,3>(0,0) = rotation.normalized().matrix() * DiagonalMatrix<float,3,3>(scale.x(), scale.y(), scale.z());

	if (t->m.isNaN().any()) {
		if (translate.isNaN().any())
			translate = float3::Zero();
		if (scale.isNaN().any() || scale.isZero())
			scale = float3::Ones();
		if (isnan(rotation.x()) || isnan(rotation.y()) || isnan(rotation.z()) || isnan(rotation.w()))
			rotation = Quaternionf(1,0,0,0);
		*t = make_transform(translate, make_quatf(rotation.x(), rotation.y(), rotation.z(), rotation.w()), scale);
	}
	
	#endif
	
	if (gAnimatedTransform == t) {
			if (ImGui::Button("Stop Animating")) gAnimatedTransform = nullptr;
			ImGui::DragFloat3("Translate", gAnimateTranslate.data(), .01f);
			ImGui::DragFloat3("Rotate", gAnimateRotate.data(), .01f);
	} else if (ImGui::Button("Animate"))
		gAnimatedTransform = t;
}
inline void inspector_gui_fn(Material* material) {
	material_inspector_gui_fn(*material);
}
inline void inspector_gui_fn(MeshPrimitive* mesh) {
	ImGui::LabelText("Material", mesh->mMaterial ? mesh->mMaterial.node().name().c_str() : "nullptr");
	ImGui::LabelText("Mesh", mesh->mMesh ? mesh->mMesh.node().name().c_str() : "nullptr");
}
inline void inspector_gui_fn(SpherePrimitive* sphere) {
	ImGui::DragFloat("Radius", &sphere->mRadius, .01f);
}

TransformData node_to_world(const Node& node) {
	static bool registered = false;
	if (!registered) {
		registered = true;

		component_ptr<Gui> gui = node.node_graph().find_components<Gui>().front();
		gui->register_inspector_gui_fn<TransformData>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<Camera>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<Material>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<MeshPrimitive>(&inspector_gui_fn);
		gui->register_inspector_gui_fn<SpherePrimitive>(&inspector_gui_fn);
		
		gAnimatedTransform = nullptr;
		node.node_graph().find_components<Application>().front()->OnUpdate.listen(gui.node(), &animate);
	}

	TransformData transform = make_transform(float3::Zero(), quatf_identity(), float3::Ones());
	const Node* p = &node;
	while (p != nullptr) {
		auto c = p->find<TransformData>();
		if (c) transform = tmul(*c, transform);
		p = p->parent();
	}
	return transform;
}

}