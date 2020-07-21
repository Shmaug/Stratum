#pragma once

#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/Window.hpp>
#include <Scene/Object.hpp>
#include <Util/Util.hpp>

// A scene object that the scene will use to render renderers
class Camera : public virtual Object {
public:
	ENGINE_EXPORT Camera(const std::string& name, ::Device* device, VkFormat renderFormat = VK_FORMAT_R8G8B8A8_UNORM, VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT, :: ClearFlags clearFlags = CLEAR_SKYBOX);
	ENGINE_EXPORT Camera(const std::string& name, ::Framebuffer* framebuffer, :: ClearFlags clearFlags = CLEAR_SKYBOX);
	ENGINE_EXPORT virtual ~Camera();

	inline ::Device* Device() const { return mDevice; }

	// Callbacks
	
	ENGINE_EXPORT virtual void DrawGui(CommandBuffer* commandBuffer, GuiContext* gui, Camera* camera) override;


	// Updates the uniform buffer
	ENGINE_EXPORT virtual void UpdateUniformBuffer();
	// Calls vkCmdSetViewport and vkCmdSetScissor
	ENGINE_EXPORT virtual void SetViewportScissor(CommandBuffer* commandBuffer);
	// Sets the viewport and StereoEye push constant
	ENGINE_EXPORT virtual void SetStereoViewport(CommandBuffer* commandBuffer, StereoEye eye);

	ENGINE_EXPORT virtual float4 WorldToClip(const float3& worldPos, StereoEye eye = EYE_NONE);
	ENGINE_EXPORT virtual float3 ClipToWorld(const float3& clipPos, StereoEye eye = EYE_NONE);
	ENGINE_EXPORT virtual Ray ScreenToWorldRay(const float2& uv, StereoEye eye = EYE_NONE);
	
	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }

	// Setters

	inline virtual void ClearFlags(::ClearFlags c) { mClearFlags = c; }
	inline virtual void StereoMode(::StereoMode s) { mStereoMode = s; Dirty(); }

	inline virtual void Orthographic(bool o) { mOrthographic = o; Dirty(); }
	inline virtual void OrthographicSize(float s) { mOrthographicSize = s; Dirty(); }

	inline virtual void Viewport(const VkViewport& v) { mViewport = v; Dirty(); }

	inline virtual void Near(float n) { mNear = n; Dirty(); }
	inline virtual void Far(float f) { mFar = f;  Dirty(); }
	inline virtual void FieldOfView(float f) { mFieldOfView = f; Dirty(); }

	inline virtual void EyeOffset(const float3& translate, const quaternion& rotate, StereoEye eye = EYE_NONE) { mEyeOffsetTranslate[eye] = translate; mEyeOffsetRotate[eye] = rotate;  Dirty(); }
	inline virtual void Projection(const float4x4& projection, StereoEye eye = EYE_NONE) { mFieldOfView = 0; mOrthographic = false; mProjection[eye] = projection; Dirty(); }

	// Getters

	inline virtual ::ClearFlags ClearFlags() { return mClearFlags; }
	inline virtual ::StereoMode StereoMode() { return mStereoMode; }

	inline virtual bool Orthographic() const { return mOrthographic; }
	inline virtual float OrthographicSize() const { return mOrthographicSize; }
	
	inline virtual VkViewport Viewport() const { return mViewport; }
	
	inline virtual float Near() const { return mNear; }
	inline virtual float Far() const { return mFar; }
	inline virtual float FieldOfView() const { return mFieldOfView; }
	inline virtual float Aspect() const { return mViewport.width / mViewport.height; }

	inline virtual float3 EyeOffsetTranslate(StereoEye eye = EYE_NONE) const { return mEyeOffsetTranslate[eye]; }
	inline virtual quaternion EyeOffsetRotate(StereoEye eye = EYE_NONE) const { return mEyeOffsetRotate[eye]; }

	inline virtual ::Framebuffer* Framebuffer() const { return mFramebuffer; }

	inline ::DescriptorSet* Camera::DescriptorSet(VkShaderStageFlags stages) { return mFrameData[mDevice->Instance()->Window()->BackBufferIndex()].mDescriptorSets.at(stages); }
	inline virtual Buffer* UniformBuffer() const { return mUniformBuffer; }


	// Note: The view matrix is calculated placing the camera at the origin. To transform from world->view, one must apply:
	// view * (worldPos-cameraPos)
	inline virtual float4x4 View(StereoEye eye = EYE_NONE) { UpdateTransform(); return mView[eye]; }
	inline virtual float4x4 InverseView(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvView[eye]; }
	inline virtual float4x4 Projection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mProjection[eye]; }
	inline virtual float4x4 InverseProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvProjection[eye]; }
	inline virtual float4x4 ViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mViewProjection[eye]; }
	inline virtual float4x4 InverseViewProjection(StereoEye eye = EYE_NONE) { UpdateTransform(); return mInvViewProjection[eye]; }

	inline virtual const float4* Frustum() { UpdateTransform(); return mFrustum; }

private:
	uint32_t mRenderPriority;

	::StereoMode mStereoMode;
	::ClearFlags mClearFlags;

	bool mOrthographic;
	float mOrthographicSize;
	float mFieldOfView; 

	float mNear;
	float mFar;

	// Per-eye data

	float4x4 mView[2];
	float4x4 mProjection[2];
	float4x4 mViewProjection[2];
	float4x4 mInvProjection[2];
	float4x4 mInvView[2];
	float4x4 mInvViewProjection[2];
	float3 mEyeOffsetTranslate[2];
	quaternion mEyeOffsetRotate[2];

	float4 mFrustum[6];

	VkViewport mViewport;

	::Device* mDevice;
	::Framebuffer* mFramebuffer;
	// If the framebuffer was not supplied to the camera on creation, then delete it
	bool mDeleteFramebuffer;

	// In flight data

	Buffer* mUniformBuffer;
	struct FrameData {
		void* mMappedUniformBuffer;
		std::unordered_map<VkShaderStageFlags, ::DescriptorSet*> mDescriptorSets;
	};
	std::vector<FrameData> mFrameData;

	void CreateDescriptorSet();

protected:
	ENGINE_EXPORT virtual bool UpdateTransform() override;
};