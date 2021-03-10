#include "Camera.hpp"
 
using namespace stm;

void Camera::WriteUniformBuffer(void* bufferData) {
	NodeTransform* transform = mNode.get_component<NodeTransform>();
	shader_interop::CameraData& buf = *(shader_interop::CameraData*)bufferData;
	buf.View = transform->Global().inverse();
	buf.Projection = mLocalProjection.matrix();
	buf.ViewProjection = buf.View * buf.Projection;
	buf.Position.head<3>() = transform->Global().col(2).head<3>();
	buf.Position.w() = mFar;
}

void Camera::SetViewportScissor(CommandBuffer& commandBuffer) {
	vk::Viewport vp = { 0.f, 0.f, (float)commandBuffer.CurrentFramebuffer()->Extent().width, (float)commandBuffer.CurrentFramebuffer()->Extent().height, 0.f, 1.f };
	// flip viewport
	vp.y += vp.height;
	vp.height = -vp.height;
	commandBuffer->setViewport(0, { vp} );

	vk::Rect2D scissor { { 0, 0 }, commandBuffer.CurrentFramebuffer()->Extent() };
	commandBuffer->setScissor(0, { scissor });
}

bool Camera::RendersToSubpass(const RenderPass::SubpassDescription& subpass) {
	for (const auto& [name, description] : subpass.mAttachmentDescriptions)
		if (mRenderTargets.count(name) && (get<RenderPass::AttachmentType>(description) & (RenderPass::AttachmentType::eColor | RenderPass::AttachmentType::eDepthStencil | RenderPass::AttachmentType::eResolve)))
			return true;
	return false;
}

void Camera::OnValidateTransform() {
	mLocalProjection = PerspectiveFov(mFieldOfView, mAspectRatio, mNear, mFar);
	
	Matrix<float,3,8> corners;
	corners.col(0) = Vector3f(-1,  1, 0);
	corners.col(1) = Vector3f( 1,  1, 0);
	corners.col(2) = Vector3f(-1, -1, 0);
	corners.col(3) = Vector3f( 1, -1, 0);
	corners.col(4) = Vector3f(-1,  1, 1);
	corners.col(5) = Vector3f( 1,  1, 1);
	corners.col(6) = Vector3f(-1, -1, 1);
	corners.col(7) = Vector3f( 1, -1, 1);
	corners = (mLocalProjection * corners.colwise().homogeneous()).colwise().hnormalized();

	mLocalFrustum[0] = Hyperplane<float,3>((corners.col(1) - corners.col(0)).cross(corners.col(2) - corners.col(0)), corners.col(0)); // near
	mLocalFrustum[1] = Hyperplane<float,3>((corners.col(6) - corners.col(4)).cross(corners.col(5) - corners.col(4)), corners.col(4)); // far
	mLocalFrustum[2] = Hyperplane<float,3>((corners.col(5) - corners.col(1)).cross(corners.col(3) - corners.col(1)), corners.col(1)); // right
	mLocalFrustum[3] = Hyperplane<float,3>((corners.col(2) - corners.col(0)).cross(corners.col(4) - corners.col(0)), corners.col(0)); // left
	mLocalFrustum[4] = Hyperplane<float,3>((corners.col(3) - corners.col(2)).cross(corners.col(6) - corners.col(2)), corners.col(2)); // top
	mLocalFrustum[5] = Hyperplane<float,3>((corners.col(4) - corners.col(0)).cross(corners.col(1) - corners.col(0)), corners.col(0)); // bottom
}