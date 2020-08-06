#pragma once

#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/Window.hpp>
#include <Scene/Object.hpp>

// A scene object that the scene will use to render renderers
class Camera : public virtual Object {
public:
	STRATUM_API Camera(const std::string& name, const std::set<RenderTargetIdentifier>& renderTargets);
	STRATUM_API virtual ~Camera();

	// Write the CameraData buffer to a location in memory
	STRATUM_API virtual void WriteUniformBuffer(void* bufferData);
	// Calls vkCmdSetViewport and vkCmdSetScissor
	STRATUM_API virtual void SetViewportScissor(CommandBuffer* commandBuffer, StereoEye eye = EYE_NONE);

	STRATUM_API virtual float4 WorldToClip(const float3& worldPos, StereoEye eye = EYE_NONE);
	STRATUM_API virtual float3 ClipToWorld(const float3& clipPos, StereoEye eye = EYE_NONE);
	STRATUM_API virtual Ray ScreenToWorldRay(const float2& uv, StereoEye eye = EYE_NONE);
	
	STRATUM_API virtual bool RendersToSubpass(RenderPass* renderPass, uint32_t subpassIndex);

	// Setters

	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }
	inline virtual void DrawSkybox(bool v) { mDrawSkybox = v; }
	inline virtual void StereoMode(::StereoMode s) { mStereoMode = s; DirtyTransform(); }
	inline virtual void Orthographic(bool o) { mOrthographic = o; DirtyTransform(); }
	inline virtual void OrthographicSize(float s) { mOrthographicSize = s; DirtyTransform(); }
	inline virtual void AspectRatio(float r) { mAspectRatio = r; DirtyTransform(); }
	inline virtual void Near(float n) { mNear = n; DirtyTransform(); }
	inline virtual void Far(float f) { mFar = f;  DirtyTransform(); }
	inline virtual void FieldOfView(float f) { mFieldOfView = f; DirtyTransform(); }
	inline virtual void EyeOffset(const float3& translate, const quaternion& rotate, StereoEye eye = EYE_NONE) { mEyeOffsetTranslate[eye] = translate; mEyeOffsetRotate[eye] = rotate;  DirtyTransform(); }
	inline virtual void Projection(const float4x4& projection, StereoEye eye = EYE_NONE) { mFieldOfView = 0; mOrthographic = false; mProjection[eye] = projection; DirtyTransform(); }

	// Getters

	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual bool DrawSkybox() const { return mDrawSkybox; }
	inline virtual ::StereoMode StereoMode() { return mStereoMode; }
	inline virtual bool Orthographic() const { return mOrthographic; }
	inline virtual float OrthographicSize() const { return mOrthographicSize; }
	inline virtual float AspectRatio() const { return mAspectRatio; }
	inline virtual float Near() const { return mNear; }
	inline virtual float Far() const { return mFar; }
	inline virtual float FieldOfView() const { return mFieldOfView; }
	inline virtual float3 EyeOffsetTranslate(StereoEye eye = EYE_NONE) const { return mEyeOffsetTranslate[eye]; }
	inline virtual quaternion EyeOffsetRotate(StereoEye eye = EYE_NONE) const { return mEyeOffsetRotate[eye]; }
	// The view matrix is calculated without translation. To transform from world->view, one must apply: view * (worldPos - cameraPos)
	inline virtual float4x4 View(StereoEye eye = EYE_NONE) { UpdateTransform(); return mView[eye]; }
	inline virtual float4x4 InverseView(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvView[eye]; }
	inline virtual float4x4 Projection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mProjection[eye]; }
	inline virtual float4x4 InverseProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvProjection[eye]; }
	inline virtual float4x4 ViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mViewProjection[eye]; }
	inline virtual float4x4 InverseViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvViewProjection[eye]; }
	inline virtual const float4* Frustum() { UpdateTransform(); return mFrustum; }

private:
	uint32_t mRenderPriority;

	std::set<RenderTargetIdentifier> mRenderTargets;

	::StereoMode mStereoMode;
	bool mDrawSkybox;

	bool mOrthographic;
	union {
		float mOrthographicSize;
		float mFieldOfView;
	};
	float mNear;
	float mFar;
	float mAspectRatio;
	
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
	STRATUM_API virtual void OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) override;
	STRATUM_API virtual bool UpdateTransform() override;
};