#include "FlyCamera.hpp"
#include "Application.hpp"
#include "Inspector.hpp"

namespace stm {

void inspector_gui_fn(Inspector& inspector, FlyCamera* v) {
	ImGui::DragFloat("Move Speed", &v->mMoveSpeed, 0.1f, 0);
	if (v->mMoveSpeed < 0) v->mMoveSpeed = 0;
	ImGui::DragFloat("Rotate", &v->mRotateSpeed, 0.001f, 0);
	if (v->mRotateSpeed < 0) v->mRotateSpeed = 0;
	ImGui::Checkbox("Match Window Rect", &v->mMatchWindowRect);
}

FlyCamera::FlyCamera(Node& node) : mNode(node) {
	component_ptr<Inspector> gui = node.node_graph().find_components<Inspector>().front();
	gui->register_inspector_gui_fn<FlyCamera>(&inspector_gui_fn);
	mNode.find_in_ancestor<Application>()->OnUpdate.add_listener(mNode, [&](CommandBuffer& commandBuffer, const float deltaTime) {
		ProfilerRegion ps("FlyCamera::update");
		const auto camera = mNode.find<Camera>();
		const auto app = mNode.find_in_ancestor<Application>();

		if (mMatchWindowRect) camera->mImageRect = vk::Rect2D{ { 0, 0 }, app->window().swapchain_extent() };

		const float aspect = camera->mImageRect.extent.height / (float)camera->mImageRect.extent.width;
		if (abs(camera->mProjection.scale[0] / camera->mProjection.scale[1] - aspect) > 1e-5) {
			const float fovy = 2 * atan(1 / camera->mProjection.scale[1]);
			camera->mProjection = make_perspective(fovy, aspect, float2::Zero(), camera->mProjection.near_plane);
		}
		const float fwd = (camera->mProjection.near_plane < 0) ? -1 : 1;

		const MouseKeyboardState& input = app->window().input_state();
		auto transform = mNode.find<TransformData>();

		if (!ImGui::GetIO().WantCaptureMouse) {
			if (input.pressed(KeyCode::eMouse2)) {
				if (input.scroll_delta() != 0)
					mMoveSpeed *= (1 + input.scroll_delta() / 8);

				mRotation.y() += input.cursor_delta().x() * fwd * mRotateSpeed;
				mRotation.x() = clamp(mRotation.x() + input.cursor_delta().y() * mRotateSpeed, -((float)M_PI) / 2, ((float)M_PI) / 2);
				const quatf r = qmul(
					angle_axis(mRotation.y(), float3(0, 1, 0)),
					angle_axis(mRotation.x(), float3(fwd, 0, 0)) );
				transform->m.block<3, 3>(0, 0) = Eigen::Quaternionf(r.w, r.xyz[0], r.xyz[1], r.xyz[2]).matrix();
			}
		}
		if (!ImGui::GetIO().WantCaptureKeyboard) {
			float3 mv = float3(0, 0, 0);
			if (input.pressed(KeyCode::eKeyD)) mv += float3(1, 0, 0);
			if (input.pressed(KeyCode::eKeyA)) mv += float3(-1, 0, 0);
			if (input.pressed(KeyCode::eKeyW)) mv += float3(0, 0, fwd);
			if (input.pressed(KeyCode::eKeyS)) mv += float3(0, 0, -fwd);
			if (input.pressed(KeyCode::eKeySpace)) mv += float3(0, 1, 0);
			if (input.pressed(KeyCode::eKeyShift)) mv += float3(0, -1, 0);
			*transform = tmul(*transform, make_transform(mv * mMoveSpeed * deltaTime, quatf_identity(), float3::Ones()));
		}
	});
}

}
