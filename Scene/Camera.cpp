#include <Scene/Camera.hpp>
#include <Scene/Scene.hpp>
#include <Shaders/include/shadercompat.h>

#include <Util/Profiler.hpp>
 
using namespace std;

void Camera::CreateDescriptorSet() {
	vector<VkShaderStageFlags> combos {
		VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	};

	VkDeviceSize sz = AlignUp<VkDeviceSize>(sizeof(CameraBuffer), mDevice->Limits().minUniformBufferOffsetAlignment);
	mUniformBuffer = new Buffer(mName + " Uniforms", mDevice, mDevice->Instance()->Window()->BackBufferCount()*sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	mFrameData.resize(mDevice->Instance()->Window()->BackBufferCount());
	for (uint32_t i = 0; i < mFrameData.size(); i++) {
		FrameData& fd = mFrameData[i];
		fd.mMappedUniformBuffer = (uint8_t*)mUniformBuffer->MappedData() + i*sz;
		for (auto& s : combos) {
			VkDescriptorSetLayoutBinding binding = {};
			binding.stageFlags = s;
			binding.binding = CAMERA_BUFFER_BINDING;
			binding.descriptorCount = 1;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			VkDescriptorSetLayoutCreateInfo dslayoutinfo = {};
			dslayoutinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslayoutinfo.bindingCount = 1;
			dslayoutinfo.pBindings = &binding;
			VkDescriptorSetLayout layout;
			vkCreateDescriptorSetLayout(*mDevice, &dslayoutinfo, nullptr, &layout);
			mDevice->SetObjectName(layout, mName + " DescriptorSetLayout", VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
			
			::DescriptorSet* ds = new ::DescriptorSet(mName + " DescriptorSet", mDevice, layout);
			ds->CreateUniformBufferDescriptor(mUniformBuffer, i*sz, sz, CAMERA_BUFFER_BINDING);
			ds->FlushWrites();
			fd.mDescriptorSets.emplace(s, ds);
		}
	}
	mViewport.x = 0;
	mViewport.y = 0;
	mViewport.width = (float)mFramebuffer->Extent().width;
	mViewport.height = (float)mFramebuffer->Extent().height;
	mViewport.minDepth = 0.f;
	mViewport.maxDepth = 1.f;
}

Camera::Camera(const string& name, Window* targetWindow, VkFormat depthFormat, VkSampleCountFlagBits sampleCount, :: ClearFlags clearFlags)
	: Object(name), mDevice(targetWindow->Device()), mTargetWindow(targetWindow), mDeleteFramebuffer(true), mClearFlags(clearFlags),
	mOrthographic(false), mOrthographicSize(3), mFieldOfView(PI/4), mNear(.03f), mFar(500.f), mRenderPriority(100), mStereoMode(STEREO_NONE) {
	
	mEyeOffsetTranslate[0] = 0;
	mEyeOffsetTranslate[1] = 0;
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);

	mTargetWindow->mTargetCamera = this;

	VkFormat fmt = targetWindow->Format().format;

	vector<VkFormat> colorFormats { fmt, VK_FORMAT_R16G16B16A16_SFLOAT };
	mFramebuffer = new ::Framebuffer(name, mDevice, targetWindow->SwapchainExtent(), colorFormats, depthFormat, sampleCount);

	mViewport.x = 0;
	mViewport.y = 0;
	mViewport.width = (float)mFramebuffer->Extent().width;
	mViewport.height = (float)mFramebuffer->Extent().height;
	
	VkClearValue c = {};
	c.color.float32[0] = 1.f;
	c.color.float32[1] = 1.f;
	c.color.float32[2] = 1.f;
	c.color.float32[3] = 1.f;
	mFramebuffer->ClearValue(1, c);

	CreateDescriptorSet();
}
Camera::Camera(const string& name, ::Device* device, VkFormat renderFormat, VkFormat depthFormat, VkSampleCountFlagBits sampleCount, :: ClearFlags clearFlags)
	: Object(name), mDevice(device), mTargetWindow(nullptr), mDeleteFramebuffer(true), mClearFlags(clearFlags),
	mOrthographic(false), mOrthographicSize(3), mFieldOfView(PI/4), mNear(.03f), mFar(500.f), mRenderPriority(100), mStereoMode(STEREO_NONE) {

	mEyeOffsetTranslate[0] = 0;
	mEyeOffsetTranslate[1] = 0;
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);

	vector<VkFormat> colorFormats{ renderFormat, VK_FORMAT_R16G16B16A16_SFLOAT };
	mFramebuffer = new ::Framebuffer(name, mDevice, { 1600, 900 }, colorFormats, depthFormat, sampleCount);

	mViewport.x = 0;
	mViewport.y = 0;
	mViewport.width = (float)mFramebuffer->Extent().width;
	mViewport.height = (float)mFramebuffer->Extent().height;

	VkClearValue c = {};
	c.color.float32[0] = 1.f;
	c.color.float32[1] = 1.f;
	c.color.float32[2] = 1.f;
	c.color.float32[3] = 1.f;
	mFramebuffer->ClearValue(1, c);

	CreateDescriptorSet();
}
Camera::Camera(const string& name, ::Framebuffer* framebuffer, :: ClearFlags clearFlags)
		: Object(name), mDevice(framebuffer->Device()), mTargetWindow(nullptr), mFramebuffer(framebuffer), mDeleteFramebuffer(false), mClearFlags(clearFlags),
	mOrthographic(false), mOrthographicSize(3), mFieldOfView(PI/4), mNear(.03f), mFar(500.f), mRenderPriority(100), mStereoMode(STEREO_NONE) {

	mViewport.x = 0;
	mViewport.y = 0;
	mViewport.width = (float)mFramebuffer->Extent().width;
	mViewport.height = (float)mFramebuffer->Extent().height;

	mEyeOffsetTranslate[0] = 0;
	mEyeOffsetTranslate[1] = 0;
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
	mEyeOffsetRotate[1] = quaternion(0,0,0,1);
	
	CreateDescriptorSet();
}

Camera::~Camera() {
	if (mTargetWindow) mTargetWindow->mTargetCamera = nullptr;
	for (uint32_t i = 0; i < mDevice->Instance()->Window()->BackBufferCount(); i++) {
		for (auto& s : mFrameData[i].mDescriptorSets) {
			vkDestroyDescriptorSetLayout(*mDevice, s.second->Layout(), nullptr);
			safe_delete(s.second);
		}
	}
	safe_delete(mUniformBuffer);
	if (mDeleteFramebuffer) safe_delete(mFramebuffer);
}

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
		clip.x *= Aspect();
		ray.mOrigin = (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz + WorldRotation() * float3(clip * mOrthographicSize, mNear);
		ray.mDirection = WorldRotation() * float3(0, 0, 1);
	} else {
		float4 p1 = mInvViewProjection[eye] * float4(clip, .1f, 1);
		ray.mDirection = normalize(p1.xyz / p1.w);
		ray.mOrigin = (ObjectToWorld() * float4(mEyeOffsetTranslate[eye], 1)).xyz + mEyeOffsetTranslate[eye];
	}
	return ray;
}

void Camera::PreBeginRenderPass() {
	if (mTargetWindow && mFramebuffer->Extent() != mTargetWindow->SwapchainExtent()) {
		mFramebuffer->Extent(mTargetWindow->SwapchainExtent());
		Dirty();

		mViewport.x = 0;
		mViewport.y = 0;
		mViewport.width = (float)mFramebuffer->Extent().width;
		mViewport.height = (float)mFramebuffer->Extent().height;
	}
	
	mFramebuffer->PreBeginRenderPass();
}

void Camera::UpdateUniformBuffer() {
	UpdateTransform();
	CameraBuffer& buf = *(CameraBuffer*)mFrameData[mDevice->Instance()->Window()->BackBufferIndex()].mMappedUniformBuffer;
	buf.View[0] = mView[0];
	buf.View[1] = mView[1];
	buf.Projection[0] = mProjection[0];
	buf.Projection[1] = mProjection[1];
	buf.ViewProjection[0] = mViewProjection[0];
	buf.ViewProjection[1] = mViewProjection[1];
	buf.InvProjection[0] = mInvProjection[0];
	buf.InvProjection[1] = mInvProjection[1];
	buf.Position[0] = ObjectToWorld() * float4(mEyeOffsetTranslate[0], 1);
	buf.Position[1] = ObjectToWorld() * float4(mEyeOffsetTranslate[1], 1);
	buf.Near = mNear;
	buf.Far = mFar;
	buf.AspectRatio = Aspect();
	buf.OrthographicSize = mOrthographic ? mOrthographicSize : 0;
}
void Camera::SetViewportScissor(CommandBuffer* commandBuffer) {
	VkRect2D scissor { { 0, 0 }, mFramebuffer->Extent() };
	vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);
	vkCmdSetViewport(*commandBuffer, 0, 1, &mViewport);
}
void Camera::BeginRenderPass(CommandBuffer* commandBuffer) {
	mFramebuffer->BeginRenderPass(commandBuffer);
	if (mClearFlags != CLEAR_NONE) mFramebuffer->Clear(commandBuffer, mClearFlags);

	UpdateUniformBuffer();
	SetViewportScissor(commandBuffer);
}

void Camera::SetStereoViewport(CommandBuffer* commandBuffer, ShaderVariant* shader, StereoEye eye) {
	VkViewport vp = mViewport;
	if (mStereoMode == STEREO_SBS_HORIZONTAL) {
		vp.width /= 2;
		vp.x = eye == EYE_LEFT ? 0 : vp.width;
	} else if (mStereoMode == STEREO_SBS_VERTICAL) {
		vp.height /= 2;
		vp.y = eye == EYE_LEFT ? 0 : vp.height;
	}

	vkCmdSetViewport(*commandBuffer, 0, 1, &vp);

	if (shader) commandBuffer->PushConstantRef(shader, "StereoEye", (uint32_t)eye);
}

bool Camera::UpdateTransform() {
	if (!Object::UpdateTransform()) return false;

	quaternion q0 = WorldRotation() * mEyeOffsetRotate[0];
	quaternion q1 = WorldRotation() * mEyeOffsetRotate[1];

	mView[0] = float4x4::Look(0, q0 * float3(0, 0, 1), q0 * float3(0, 1, 0));
	mView[1] = float4x4::Look(0, q1 * float3(0, 0, 1), q1 * float3(0, 1, 0));

	if (mOrthographic)
		mProjection[0] = mProjection[1] = float4x4::Orthographic(mOrthographicSize * Aspect(), mOrthographicSize, mNear, mFar);
	else if (mFieldOfView)
		mProjection[0] = mProjection[1] = float4x4::PerspectiveFov(mFieldOfView, Aspect(), mNear, mFar);
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

void Camera::DrawGUI(CommandBuffer* commandBuffer, Camera* camera) {
	if (camera == this) return;
	GUI::WireSphere(WorldPosition(), mNear, 1.f);

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
	GUI::PolyLine(float4x4(1), points.data(), points.size(), color, 1.f);

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
		GUI::PolyLine(float4x4(1), points2.data(), points2.size(), float4(.5f, .5f, 1, .5f), 1.f);
	}
}