#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>

#include <Util/Profiler.hpp>
 
using namespace std;

Camera::Camera(const string& name, const set<RenderTargetIdentifier>& renderTargets)
	: Object(name), mRenderTargets(renderTargets), mDrawSkybox(true), mOrthographic(false), mFieldOfView(PI/4), mNear(.0625f), mFar(1024.f), mAspectRatio(1), mRenderPriority(100), mStereoMode(STEREO_NONE) {
	mEyeOffsetTranslate[0] = 0;
	mEyeOffsetTranslate[1] = 0;
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
}
Camera::~Camera() {}

float4 Camera::WorldToClip(const float3& worldPos, StereoEye eye) {
	UpdateTransform();
	return mViewProjection[eye] * float4((worldPos - (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz), 1);
}
float3 Camera::ClipToWorld(const float3& clipPos, StereoEye eye) {
	UpdateTransform();
	float4 wp = mInvViewProjection[eye] * float4(clipPos, 1);
	wp.xyz /= wp.w;
	return (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz + wp.xyz;
}
Ray Camera::ScreenToWorldRay(const float2& uv, StereoEye eye) {
	UpdateTransform();
	float2 clip = 2.f * uv - 1.f;
	Ray ray;
	if (mOrthographic) {
		clip.x *= mAspectRatio;
		ray.mOrigin = (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz + WorldRotation() * float3(clip * mOrthographicSize, mNear);
		ray.mDirection = WorldRotation() * float3(0, 0, 1);
	} else {
		float4 p1 = mInvViewProjection[eye] * float4(clip, .1f, 1);
		ray.mDirection = normalize(p1.xyz / p1.w);
		ray.mOrigin = (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz + mEyeOffsetTranslate[eye];
	}
	return ray;
}

void Camera::WriteUniformBuffer(void* bufferData) {
	UpdateTransform();
	CameraBuffer& buf = *(CameraBuffer*)bufferData;
	buf.View[0] = mView[0];
	buf.View[1] = mView[1];
	buf.Projection[0] = mProjection[0];
	buf.Projection[1] = mProjection[1];
	buf.ViewProjection[0] = mViewProjection[0];
	buf.ViewProjection[1] = mViewProjection[1];
	buf.InvProjection[0] = mInvProjection[0];
	buf.InvProjection[1] = mInvProjection[1];
	buf.Position[0] = float4((ObjectToWorld() * float4(mEyeOffsetTranslate[0], 1)).xyz, mNear);
	buf.Position[1] = float4((ObjectToWorld() * float4(mEyeOffsetTranslate[1], 1)).xyz, mFar);
}
void Camera::SetViewportScissor(CommandBuffer* commandBuffer, StereoEye eye) {
	VkViewport vp = { 0.f, 0.f, (float)commandBuffer->CurrentFramebuffer()->Extent().width, (float)commandBuffer->CurrentFramebuffer()->Extent().height, 0.f, 1.f };
	if (mStereoMode == STEREO_SBS_HORIZONTAL) {
		vp.width /= 2;
		vp.x = eye == EYE_LEFT ? 0 : vp.width;
	} else if (mStereoMode == STEREO_SBS_VERTICAL) {
		vp.height /= 2;
		vp.y = eye == EYE_LEFT ? 0 : vp.height;
	}
	// make viewport go from y-down (vulkan) to y-up
	vp.y += vp.height;
	vp.height = -vp.height;
	vkCmdSetViewport(*commandBuffer, 0, 1, &vp);
	commandBuffer->PushConstantRef("StereoEye", (uint32_t)eye);

	VkRect2D scissor { { 0, 0 }, commandBuffer->CurrentFramebuffer()->Extent() };
	vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);
}

bool Camera::RendersToSubpass(RenderPass* renderPass, uint32_t subpassIndex) {
	const Subpass& subpass = renderPass->GetSubpass(subpassIndex);
	for (auto& kp : subpass.mAttachments)
			if ((kp.second.mType & (ATTACHMENT_COLOR | ATTACHMENT_DEPTH_STENCIL | ATTACHMENT_RESOLVE)) && mRenderTargets.count(kp.first)) return true;
	return false;
}

bool Camera::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;

	quaternion q0 = WorldRotation() * mEyeOffsetRotate[0];
	quaternion q1 = WorldRotation() * mEyeOffsetRotate[1];

	mView[0] = float4x4::Look(0, q0 * float3(0, 0, 1), q0 * float3(0, 1, 0));
	mView[1] = float4x4::Look(0, q1 * float3(0, 0, 1), q1 * float3(0, 1, 0));

	if (mOrthographic)
		mProjection[0] = mProjection[1] = float4x4::Orthographic(mOrthographicSize * mAspectRatio, mOrthographicSize, mNear, mFar);
	else if (mFieldOfView)
		mProjection[0] = mProjection[1] = float4x4::PerspectiveFov(mFieldOfView, mAspectRatio, mNear, mFar);
	// else Projection has been set manually

	mViewProjection[0] = mProjection[0] * mView[0];
	mViewProjection[1] = mProjection[1] * mView[1];

	mInvView[0] = inverse(mView[0]);
	mInvView[1] = inverse(mView[1]);
	mInvProjection[0] = inverse(mProjection[0]);
	mInvProjection[1] = inverse(mProjection[1]);
	mInvViewProjection[0] = inverse(mViewProjection[0]);
	mInvViewProjection[1] = inverse(mViewProjection[1]);
	
	float3 corners[8] {
		float3(-1,  1, 0),
		float3( 1,  1, 0),
		float3(-1, -1, 0),
		float3( 1, -1, 0),
		
		float3(-1,  1, 1),
		float3( 1,  1, 1),
		float3(-1, -1, 1),
		float3( 1, -1, 1),
	};
	for (uint32_t i = 0; i < 8; i++) {
		float4 c = mInvViewProjection[0] * float4(corners[i], 1);
		corners[i] = c.xyz / c.w + WorldPosition();
	}

	mFrustum[0].xyz = normalize(cross(corners[1] - corners[0], corners[2] - corners[0])); // near
	mFrustum[1].xyz = normalize(cross(corners[6] - corners[4], corners[5] - corners[4])); // far
	mFrustum[2].xyz = normalize(cross(corners[5] - corners[1], corners[3] - corners[1])); // right
	mFrustum[3].xyz = normalize(cross(corners[2] - corners[0], corners[4] - corners[0])); // left
	mFrustum[4].xyz = normalize(cross(corners[3] - corners[2], corners[6] - corners[2])); // top
	mFrustum[5].xyz = normalize(cross(corners[4] - corners[0], corners[1] - corners[0])); // bottom

	mFrustum[0].w = dot(mFrustum[0].xyz, corners[0]);
	mFrustum[1].w = dot(mFrustum[1].xyz, corners[4]);
	mFrustum[2].w = dot(mFrustum[2].xyz, corners[1]);
	mFrustum[3].w = dot(mFrustum[3].xyz, corners[0]);
	mFrustum[4].w = dot(mFrustum[4].xyz, corners[2]);
	mFrustum[5].w = dot(mFrustum[5].xyz, corners[0]);

	return true;
}

void Camera::OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) {
	if (camera == this) return;
	
	gui->WireSphere(WorldPosition(), mNear, 1.f);

	float3 f0 = ClipToWorld(float3(-1, -1, 0));
	float3 f1 = ClipToWorld(float3(-1, 1, 0));
	float3 f2 = ClipToWorld(float3(1, -1, 0));
	float3 f3 = ClipToWorld(float3(1, 1, 0));

	float3 f4 = ClipToWorld(float3(-1, -1, 1));
	float3 f5 = ClipToWorld(float3(-1, 1, 1));
	float3 f6 = ClipToWorld(float3(1, -1, 1));
	float3 f7 = ClipToWorld(float3(1, 1, 1));

	float4 color = mStereoMode == STEREO_NONE ? 1 : float4(1, .5f, .5f, .5f);

	vector<float3> points {
		f0, f4, f5, f1, f0,
		f2, f6, f7, f3, f2,
		f6, f4, f5, f7, f3, f1
	};
	gui->PolyLine(float4x4(1), points.data(), (uint32_t)points.size(), color, 1.f);

	if (mStereoMode != STEREO_NONE) {
		f0 = ClipToWorld(float3(-1, -1, 0), EYE_RIGHT);
		f1 = ClipToWorld(float3(-1, 1, 0), EYE_RIGHT);
		f2 = ClipToWorld(float3(1, -1, 0), EYE_RIGHT);
		f3 = ClipToWorld(float3(1, 1, 0), EYE_RIGHT);

		f4 = ClipToWorld(float3(-1, -1, 1), EYE_RIGHT);
		f5 = ClipToWorld(float3(-1, 1, 1), EYE_RIGHT);
		f6 = ClipToWorld(float3(1, -1, 1), EYE_RIGHT);
		f7 = ClipToWorld(float3(1, 1, 1), EYE_RIGHT);

		vector<float3> points2 {
			f0, f4, f5, f1, f0, f2, 
		};
		gui->PolyLine(float4x4(1), points2.data(), (uint32_t)points2.size(), float4(.5f, .5f, 1, .5f), 1.f);
	}
}