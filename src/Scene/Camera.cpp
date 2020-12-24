#include "Camera.hpp"
 

using namespace stm;

Camera::Camera(const string& name, stm::Scene& scene, const set<RenderTargetIdentifier>& renderTargets) : Object(name, scene), mRenderTargets(renderTargets), mFieldOfView((float)M_PI/4) {
	mEyeOffsetTranslate[0] = 0;
	mEyeOffsetTranslate[1] = 0;
	mEyeOffsetRotate[1] = fquat(0,0,0,1);
	mEyeOffsetRotate[1] = fquat(0,0,0,1);
}
Camera::~Camera() {}

float4 Camera::WorldToClip(const float3& worldPos, StereoEye eye) {
	ValidateTransform();
	return mViewProjection[(uint32_t)eye] * float4((worldPos - (float3)(Transform() * float4(mEyeOffsetTranslate[(uint32_t)eye], 1))), 1);
}
float3 Camera::ClipToWorld(const float3& clipPos, StereoEye eye) {
	ValidateTransform();
	float4 wp = mInvViewProjection[(uint32_t)eye] * float4(clipPos, 1);
	wp /= wp.w;
	return (float3)(Transform() * float4(mEyeOffsetTranslate[(uint32_t)eye], 1)) + (float3)wp;
}
fRay Camera::ScreenToWorldRay(const float2& uv, StereoEye eye) {
	ValidateTransform();
	float2 clip = 2.f * uv - 1.f;
	fRay ray;
	if (mOrthographic) {
		clip.x *= mAspectRatio;
		ray.mOrigin = (float3)(Transform() * float4(mEyeOffsetTranslate[(uint32_t)eye], 1)) + Rotation() * float3(clip * mOrthographicSize, mNear);
		ray.mDirection = Rotation() * float3(0, 0, 1);
	} else {
		float4 p1 = mInvViewProjection[(uint32_t)eye] * float4(clip, .1f, 1);
		ray.mDirection = normalize((float3)p1/p1.w);
		ray.mOrigin = (float3)(Transform() * float4(mEyeOffsetTranslate[(uint32_t)eye], 1)) + mEyeOffsetTranslate[(uint32_t)eye];
	}
	return ray;
}

void Camera::WriteUniformBuffer(void* bufferData) {
	ValidateTransform();
	CameraData& buf = *(CameraData*)bufferData;
	buf.View[0] = mView[0];
	buf.View[1] = mView[1];
	buf.Projection[0] = mProjection[0];
	buf.Projection[1] = mProjection[1];
	buf.ViewProjection[0] = mViewProjection[0];
	buf.ViewProjection[1] = mViewProjection[1];
	buf.InvProjection[0] = mInvProjection[0];
	buf.InvProjection[1] = mInvProjection[1];
	buf.Position[0] = float4((float3)(Transform() * float4(mEyeOffsetTranslate[0], 1)), mNear);
	buf.Position[1] = float4((float3)(Transform() * float4(mEyeOffsetTranslate[1], 1)), mFar);
}
void Camera::SetViewportScissor(CommandBuffer& commandBuffer, StereoEye eye) {
	vk::Viewport vp = { 0.f, 0.f, (float)commandBuffer.CurrentFramebuffer()->Extent().width, (float)commandBuffer.CurrentFramebuffer()->Extent().height, 0.f, 1.f };
	if (mStereoMode == StereoMode::eHorizontal) {
		vp.width /= 2;
		vp.x = eye == StereoEye::eLeft ? 0 : vp.width;
	} else if (mStereoMode == StereoMode::eVertical) {
		vp.height /= 2;
		vp.y = eye == StereoEye::eLeft ? 0 : vp.height;
	}
	// make viewport go from y-down (vulkan) to y-up
	vp.y += vp.height;
	vp.height = -vp.height;
	commandBuffer->setViewport(0, { vp} );
	commandBuffer.PushConstantRef("StereoEye", (uint32_t)eye);

	vk::Rect2D scissor { { 0, 0 }, commandBuffer.CurrentFramebuffer()->Extent() };
	commandBuffer->setScissor(0, { scissor });
}

bool Camera::RendersToSubpass(RenderPass& renderPass, uint32_t subpassIndex) {
	const Subpass& subpass = renderPass.Subpass(subpassIndex);
	for (auto& kp : subpass.mAttachments)
			if (mRenderTargets.count(kp.first) && 
				 (kp.second.mType == AttachmentType::eColor || 
					kp.second.mType == AttachmentType::eDepthStencil ||
					kp.second.mType == AttachmentType::eResolve)) return true;
	return false;
}

bool Camera::ValidateTransform() {
	if (!Object::ValidateTransform()) return false;

	fquat q0 = Rotation() * mEyeOffsetRotate[0];
	fquat q1 = Rotation() * mEyeOffsetRotate[1];

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
		corners[i] = (float3)c / c.w + Position();
	}

	mFrustum[0] = float4(normalize(cross(corners[1] - corners[0], corners[2] - corners[0])), 0); // near
	mFrustum[1] = float4(normalize(cross(corners[6] - corners[4], corners[5] - corners[4])), 0); // far
	mFrustum[2] = float4(normalize(cross(corners[5] - corners[1], corners[3] - corners[1])), 0); // right
	mFrustum[3] = float4(normalize(cross(corners[2] - corners[0], corners[4] - corners[0])), 0); // left
	mFrustum[4] = float4(normalize(cross(corners[3] - corners[2], corners[6] - corners[2])), 0); // top
	mFrustum[5] = float4(normalize(cross(corners[4] - corners[0], corners[1] - corners[0])), 0); // bottom

	mFrustum[0].w = dot((float3)mFrustum[0], corners[0]);
	mFrustum[1].w = dot((float3)mFrustum[1], corners[4]);
	mFrustum[2].w = dot((float3)mFrustum[2], corners[1]);
	mFrustum[3].w = dot((float3)mFrustum[3], corners[0]);
	mFrustum[4].w = dot((float3)mFrustum[4], corners[2]);
	mFrustum[5].w = dot((float3)mFrustum[5], corners[0]);

	return true;
}

void Camera::OnGui(CommandBuffer& commandBuffer, GuiContext& gui) {	
	gui.WireSphere(Position(), mNear, Rotation(), 1.f);

	float3 f0 = ClipToWorld(float3(-1, -1, 0));
	float3 f1 = ClipToWorld(float3(-1, 1, 0));
	float3 f2 = ClipToWorld(float3(1, -1, 0));
	float3 f3 = ClipToWorld(float3(1, 1, 0));

	float3 f4 = ClipToWorld(float3(-1, -1, 1));
	float3 f5 = ClipToWorld(float3(-1, 1, 1));
	float3 f6 = ClipToWorld(float3(1, -1, 1));
	float3 f7 = ClipToWorld(float3(1, 1, 1));

	float4 color = mStereoMode == StereoMode::eNone ? 1 : float4(1, .5f, .5f, .5f);

	vector<float3> points {
		f0, f4, f5, f1, f0,
		f2, f6, f7, f3, f2,
		f6, f4, f5, f7, f3, f1
	};
	gui.PolyLine({ {}, {}, float4x4(1) }, points.data(), (uint32_t)points.size(), 1.f);

	if (mStereoMode != StereoMode::eNone) {
		f0 = ClipToWorld(float3(-1, -1, 0), StereoEye::eRight);
		f1 = ClipToWorld(float3(-1, 1, 0), StereoEye::eRight);
		f2 = ClipToWorld(float3(1, -1, 0), StereoEye::eRight);
		f3 = ClipToWorld(float3(1, 1, 0), StereoEye::eRight);

		f4 = ClipToWorld(float3(-1, -1, 1), StereoEye::eRight);
		f5 = ClipToWorld(float3(-1, 1, 1), StereoEye::eRight);
		f6 = ClipToWorld(float3(1, -1, 1), StereoEye::eRight);
		f7 = ClipToWorld(float3(1, 1, 1), StereoEye::eRight);

		vector<float3> points2 { f0, f4, f5, f1, f0, f2 };
		gui.PolyLine({ {}, {}, float4x4(1) }, points2.data(), (uint32_t)points2.size(), 1.f);
	}
}