#pragma once

#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/Window.hpp>
#include <Scene/Object.hpp>

namespace stm {

// A scene object that the scene will use to render renderers
class Camera : public Object {
public:
	STRATUM_API Camera(const std::string& name, Scene* scene, const std::set<RenderTargetIdentifier>& renderTargets);
	STRATUM_API virtual ~Camera();

	// Write the CameraData buffer to a location in memory
	STRATUM_API virtual void WriteUniformBuffer(void* bufferData);
	// Calls vkCmdSetViewport and vkCmdSetScissor
	STRATUM_API virtual void SetViewportScissor(CommandBuffer& commandBuffer, StereoEye eye = StereoEye::eNone);

	STRATUM_API virtual float4 WorldToClip(const float3& worldPos, StereoEye eye = StereoEye::eNone);
	STRATUM_API virtual float3 ClipToWorld(const float3& clipPos, StereoEye eye = StereoEye::eNone);
	STRATUM_API virtual Ray ScreenToWorldRay(const float2& uv, StereoEye eye = StereoEye::eNone);
	
	STRATUM_API virtual bool RendersToSubpass(RenderPass& renderPass, uint32_t subpassIndex);

	// Setters

	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }
	inline virtual void DrawSkybox(bool v) { mDrawSkybox = v; }
	inline virtual void StereoMode(stm::StereoMode s) { mStereoMode = s; DirtyTransform(); }
	inline virtual void Orthographic(bool o) { mOrthographic = o; DirtyTransform(); }
	inline virtual void OrthographicSize(float s) { mOrthographicSize = s; DirtyTransform(); }
	inline virtual void AspectRatio(float r) { mAspectRatio = r; DirtyTransform(); }
	inline virtual void Near(float n) { mNear = n; DirtyTransform(); }
	inline virtual void Far(float f) { mFar = f;  DirtyTransform(); }
	inline virtual void FieldOfView(float f) { mFieldOfView = f; DirtyTransform(); }
	inline virtual void EyeOffset(const float3& translate, const quaternion& rotate, StereoEye eye = StereoEye::eNone) { mEyeOffsetTranslate[(uint32_t)eye] = translate; mEyeOffsetRotate[(uint32_t)eye] = rotate;  DirtyTransform(); }
	inline virtual void Projection(const float4x4& projection, StereoEye eye = StereoEye::eNone) { mFieldOfView = 0; mOrthographic = false; mProjection[(uint32_t)eye] = projection; DirtyTransform(); }

	// Getters

	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual bool DrawSkybox() const { return mDrawSkybox; }
	inline virtual stm::StereoMode StereoMode() { return mStereoMode; }
	inline virtual bool Orthographic() const { return mOrthographic; }
	inline virtual float OrthographicSize() const { return mOrthographicSize; }
	inline virtual float AspectRatio() const { return mAspectRatio; }
	inline virtual float Near() const { return mNear; }
	inline virtual float Far() const { return mFar; }
	inline virtual float FieldOfView() const { return mFieldOfView; }
	inline virtual float3 EyeOffsetTranslate(StereoEye eye = StereoEye::eNone) const { return mEyeOffsetTranslate[(uint32_t)eye]; }
	inline virtual quaternion EyeOffsetRotate(StereoEye eye = StereoEye::eNone) const { return mEyeOffsetRotate[(uint32_t)eye]; }
	// The view matrix is calculated without translation. To transform from world->view, one must apply: view * (worldPos - cameraPos)
	inline virtual float4x4 View(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mView[(uint32_t)eye]; }
	inline virtual float4x4 InverseView(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mInvView[(uint32_t)eye]; }
	inline virtual float4x4 Projection(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mProjection[(uint32_t)eye]; }
	inline virtual float4x4 InverseProjection(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mInvProjection[(uint32_t)eye]; }
	inline virtual float4x4 ViewProjection(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mViewProjection[(uint32_t)eye]; }
	inline virtual float4x4 InverseViewProjection(StereoEye eye = StereoEye::eNone) { UpdateTransform(); return mInvViewProjection[(uint32_t)eye]; }
	inline virtual const float4* Frustum() { UpdateTransform(); return mFrustum; }

private:
	uint32_t mRenderPriority = 100;
	std::set<RenderTargetIdentifier> mRenderTargets;

	stm::StereoMode mStereoMode = stm::StereoMode::eNone;
	bool mDrawSkybox = true;

	bool mOrthographic = false;
	union {
		float mOrthographicSize;
		float mFieldOfView;
	};
	float mNear = 0.01f;
	float mFar = 1024;
	float mAspectRatio = 1.f;
	
	float4 mFrustum[6];

	// Per-eye data

	float4x4 mView[2];
	float4x4 mProjection[2];
	float4x4 mViewProjection[2];
	float4x4 mInvProjection[2];
	float4x4 mInvView[2];
	float4x4 mInvViewProjection[2];
	float3 mEyeOffsetTranslate[2];
	quaternion mEyeOffsetRotate[2];

protected:
	STRATUM_API virtual void OnGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui) override;
	STRATUM_API virtual bool UpdateTransform() override;
};

}