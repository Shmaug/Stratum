#pragma once

#include "Scene.hpp"

namespace stm {

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
class Camera : public NodeTransform {
public:
	inline Camera(SceneNode& node, const string& name, const unordered_set<RenderAttachmentId>& renderTargets) : SceneNode::Component(node, name), mRenderTargets(renderTargets) {}

	// Write the CameraData buffer to a location in memory
	STRATUM_API virtual void WriteUniformBuffer(void* bufferData);
	// Calls vkCmdSetViewport and vkCmdSetScissor
	STRATUM_API virtual void SetViewportScissor(CommandBuffer& commandBuffer);

	STRATUM_API virtual bool RendersToSubpass(const RenderPass::SubpassDescription& subpass);

	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }
	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual void DrawSkybox(bool v) { mDrawSkybox = v; }
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
	STRATUM_API virtual void OnValidateTransform() override;
};

}