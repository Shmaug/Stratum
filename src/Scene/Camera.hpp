#pragma once

#include "Scene.hpp"

namespace stm {

template<typename T> inline Transform<T,3,Projective> Perspective(T width, T height, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2*zNear/width;
	r(1,1) = 2*zNear/height;
	r(2,2) = zFar / (zFar - zNear);
	r(2,3) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Perspective(T left, T right, T top, T bottom, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2*zNear / (right - left);
	r(1,1) = 2*zNear / (top - bottom);
	r(2,0) = (left + right) / (left - right);
	r(1,2) = (top + bottom) / (bottom - top);
	r(2,2) = zFar / (zFar - zNear);
	r(2,3) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> PerspectiveFov(T fovy, T aspect, T zNear, T zFar) {
	T sy = 1 / tan(fovy / 2);
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = sy/aspect;
	r(1,1) = sy;
	r(2,2) = zFar / (zFar - zNear);
	r(3,2) = zNear * -r(2,2);
	r(2,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Orthographic(T width, T height, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2/width;
	r(1,1) = 2/height;
	r(2,2) = 1/(zFar - zNear);
	r(3,2) = -zNear * r(2,2);
	r(3,3) = 1;
	return Transform<T,3,Projective>(r);
}
template<typename T> inline Transform<T,3,Projective> Orthographic(T left, T right, T bottom, T top, T zNear, T zFar) {
	Matrix<T,4,4> r = Matrix<T,4,4>::Zero();
	r(0,0) = 2 / (right - left);
	r(1,1) = 2 / (top - bottom);
	r(2,2) = 1 / (zFar - zNear);
	r(3,0) = (left + right) / (left - right);
	r(3,1) = (top + bottom) / (bottom - top);
	r(3,2) = zNear / (zNear - zFar);
	r(3,3) = 1;
	return Transform<T,3,Projective>(r);
}

enum class StereoEye : uint32_t {
	eNone = 0,
	eLeft = 0,
	eRight = 1
};
enum class StereoMode : uint32_t {
	eNone = 0,
	eVertical = 1,
	eHorizontal = 2
};

// A scene object that the scene will use to render renderers
class Camera : public SceneNode::Component {
public:
	inline Camera(SceneNode& node, const string& name, const unordered_set<RenderAttachmentId>& renderTargets) : SceneNode::Component(node, name), mRenderTargets(renderTargets) {}

	// Write the CameraData buffer to a location in memory
	STRATUM_API virtual void WriteUniformBuffer(void* bufferData);
	// Calls vkCmdSetViewport and vkCmdSetScissor
	STRATUM_API virtual void SetViewportScissor(CommandBuffer& commandBuffer);

	STRATUM_API virtual bool RendersToSubpass(const RenderPass::SubpassDescription& subpass);

	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }
	inline virtual void DrawSkybox(bool v) { mDrawSkybox = v; }
	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual bool DrawSkybox() const { return mDrawSkybox; }

	inline virtual void AspectRatio(float r) { mAspectRatio = r; mNode.InvalidateTransform(); }
	inline virtual float AspectRatio() const { return mAspectRatio; }
	inline virtual void Near(float n) { mNear = n; mNode.InvalidateTransform(); }
	inline virtual float Near() const { return mNear; }
	inline virtual void Far(float f) { mFar = f;  mNode.InvalidateTransform(); }
	inline virtual float Far() const { return mFar; }

	inline virtual const std::array<Hyperplane<float,3>,6>& LocalFrustum() { mNode.ValidateTransform(); return mLocalFrustum; }

private:
	uint32_t mRenderPriority = 100;
	unordered_set<RenderAttachmentId> mRenderTargets;

	bool mDrawSkybox = true;

	float mNear = 0.01f;
	float mFar = 1024;
	float mAspectRatio = 1;
	float mFieldOfView = 1;

	Projective3f mLocalProjection;
	std::array<Hyperplane<float,3>,6> mLocalFrustum;

protected:
	STRATUM_API virtual void OnValidateTransform(Matrix4f& transform, TransformTraits& traits) override;
};

}