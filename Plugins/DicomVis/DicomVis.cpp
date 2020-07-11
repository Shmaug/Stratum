#include <Scene/MeshRenderer.hpp>
#include <Content/Font.hpp>
#include <Scene/GUI.hpp>
#include <Util/Profiler.hpp>

#include <Core/EnginePlugin.hpp>
#include <assimp/pbrmaterial.h>

#include "ImageLoader.hpp"

using namespace std;

enum MaskValue {
	MASK_NONE = 0,
	MASK_BLADDER = 1,
	MASK_KIDNEY = 2,
	MASK_COLON = 4,
	MASK_SPLEEN = 8,
	MASK_ILEUM = 16,
	MASK_AORTA = 32,
	MASK_ALL = 63,
};

class DicomVis : public EnginePlugin {
private:
	Scene* mScene;
	vector<Object*> mObjects;
	Camera* mMainCamera;
	Camera* mRenderCamera;

	uint32_t mFrameIndex;

	float3 mVolumePosition;
	quaternion mVolumeRotation;
	float3 mVolumeScale;

	// Render parameters

	bool mLighting;
	bool mColorize;
	float mStepSize;
	float mDensity;
	uint32_t mMaskValue;
	float2 mHueRange;
	float2 mRemapRange;


	// The volume loaded directly from the folder
	Texture* mRawVolume;
	// The mask loaded directly from the folder
	Texture* mRawMask;
	// The baked volume. This CAN be nullptr, in which case the shader will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume;
	// The gradient of the volume. This CAN be nullptr, in which case the shader will compute the gradient on the fly.
	Texture* mGradient;

	Texture* mHistoryBuffer;


	// Information about the state of the volume textures

	bool mRawVolumeColored;
	bool mBakeDirty;
	bool mGradientDirty;
	
	MouseKeyboardInput* mKeyboardInput;

	float mZoom;

	bool mShowPerformance;

	std::thread mScanThread;
	bool mScanDone;

	std::unordered_map<std::string, ImageStackType> mDataFolders;

	PLUGIN_EXPORT void ScanFolders() {
		string path = "/Data";
		for (uint32_t i = 0; i < mScene->Instance()->CommandLineArguments().size(); i++)
			if (mScene->Instance()->CommandLineArguments()[i] == "--datapath") {
				i++;
				if (i < mScene->Instance()->CommandLineArguments().size())
					path = mScene->Instance()->CommandLineArguments()[i];
			}
		if (!fs::exists(path)) path = "/Data";
		if (!fs::exists(path)) path = "/data";
		if (!fs::exists(path)) path = "~/Data";
		if (!fs::exists(path)) path = "~/data";
		if (!fs::exists(path)) path = "C:/Data";
		if (!fs::exists(path)) path = "D:/Data";
		if (!fs::exists(path)) path = "E:/Data";
		if (!fs::exists(path)) path = "F:/Data";
		if (!fs::exists(path)) path = "G:/Data";
		if (!fs::exists(path)) {
			fprintf_color(COLOR_RED, stderr, "DicomVis: Could not locate datapath. Please specify with --datapath <path>\n");
			return;
		}

		for (const auto& p : fs::recursive_directory_iterator(path)) {
			if (!p.is_directory() || p.path().stem() == "_mask") continue;
			ImageStackType type = ImageLoader::FolderStackType(p.path());
			if (type == IMAGE_STACK_NONE) continue;
			mDataFolders[p.path().string()] = type;
		}

		mScanDone = true;
	}

public:
	PLUGIN_EXPORT DicomVis(): mScene(nullptr), mShowPerformance(false),
		mFrameIndex(0), mRawVolume(nullptr), mRawMask(nullptr), mBakedVolume(nullptr), mGradient(nullptr), mBakeDirty(false), mGradientDirty(false),
		mColorize(false), mLighting(false), mHistoryBuffer(nullptr), mRenderCamera(nullptr),
		mVolumePosition(float3(0,0,0)), mVolumeRotation(quaternion(0,0,0,1)),
		mDensity(500.f), mMaskValue(MASK_ALL), mRemapRange(float2(.125f, 1.f)), mHueRange(float2(.01f, .5f)), mStepSize(.001f){
		mEnabled = true;
	}
	PLUGIN_EXPORT ~DicomVis() {
		if (mScanThread.joinable()) mScanThread.join();
		safe_delete(mHistoryBuffer);
		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mGradient);
		safe_delete(mBakedVolume);
		for (Object* obj : mObjects)
			mScene->RemoveObject(obj);
	}

	PLUGIN_EXPORT bool Init(Scene* scene) override {
		mScene = scene;
		mKeyboardInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;

		shared_ptr<Camera> camera = make_shared<Camera>("Camera", mScene->Instance()->Window());
		camera->Near(.01f);
		camera->Far(800.f);
		camera->FieldOfView(radians(65.f));
		camera->LocalPosition(0, 1.6f, -mZoom);
		mMainCamera = camera.get();
		mScene->AddObject(camera);
		mObjects.push_back(mMainCamera);
		mRenderCamera = mMainCamera;
		mRenderCamera->Framebuffer()->ColorBufferUsage(mRenderCamera->Framebuffer()->ColorBufferUsage() | VK_IMAGE_USAGE_STORAGE_BIT);

		mScene->AmbientLight(.5f);

		mScanDone = false;
		mScanThread = thread(&DicomVis::ScanFolders, this);

		return true;
	}
	PLUGIN_EXPORT void Update(CommandBuffer* commandBuffer) override {
		if (mKeyboardInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;

		// Prefer a stereo camera over the main camera
		mRenderCamera = mMainCamera;
		for (Camera* c : mScene->Cameras())
			if (c->EnabledHierarchy() && c->StereoMode() != STEREO_NONE) {
				mRenderCamera = c;
				break;
			}

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->ScrollDelta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->ScrollDelta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);

				mFrameIndex = 0;
			}
			if (mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x + mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f){
					mVolumeRotation = quaternion(length(axis) * .003f, -normalize(axis)) * mVolumeRotation;
					mFrameIndex = 0;
				}
			}
		}
	}

	PLUGIN_EXPORT void DrawGUI(CommandBuffer* commandBuffer, Camera* camera) override {
		bool worldSpace = camera->StereoMode() != STEREO_NONE;

		// Draw performance overlay
		#ifdef PROFILER_ENABLE
		if (mShowPerformance && !worldSpace)
			Profiler::DrawProfiler(mScene);
		#endif

		if (!mScanDone)  return;
		if (mScanThread.joinable()) mScanThread.join();

		if (worldSpace)
			GUI::BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 300, 850));
		else
			GUI::BeginScreenLayout(LAYOUT_VERTICAL, fRect2D(10, camera->Framebuffer()->Extent().height - 460, 300, 450));

		GUI::LayoutTitle("Load Dataset");
		GUI::LayoutSeparator();
		float prev = GUI::mLayoutTheme.mControlSize;
		GUI::mLayoutTheme.mControlSize = 24;
		GUI::BeginScrollSubLayout(175, mDataFolders.size() * (GUI::mLayoutTheme.mControlSize + 2*GUI::mLayoutTheme.mControlPadding));
		for (const auto& p : mDataFolders)
			if (GUI::LayoutTextButton(fs::path(p.first).stem().string(), TEXT_ANCHOR_MIN))
				LoadVolume(commandBuffer, p.first, p.second);
		GUI::mLayoutTheme.mControlSize = prev;
		GUI::EndLayout();

		GUI::LayoutTitle("Render Settings");
		if (GUI::LayoutToggle("Colorize", mColorize)) {
			mBakeDirty = true;
			mFrameIndex = 0;
		}
		if (GUI::LayoutToggle("Lighting", mLighting)) {
			mFrameIndex = 0;
		}
		if (GUI::LayoutSlider("Step Size", mStepSize, .0001f, .01f)) mFrameIndex = 0;
		if (GUI::LayoutSlider("Density", mDensity, 10, 50000.f)) mFrameIndex = 0;
		if (GUI::LayoutRangeSlider("Remap", mRemapRange, 0, 1)) {
			mBakeDirty = true;
			mFrameIndex = 0;
		}
		if (mColorize) {
			if (GUI::LayoutRangeSlider("Hue Range", mHueRange, 0, 1)) {
				mBakeDirty = true;
				mFrameIndex = 0;
			}
		}

		GUI::EndLayout();
	}

	PLUGIN_EXPORT void PostEndRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override {
		if (!mRawVolume || camera != mRenderCamera) return;

		if (!mHistoryBuffer || camera->Framebuffer()->Extent() != To2D(mHistoryBuffer->Extent())) {
			safe_delete(mHistoryBuffer);
			mHistoryBuffer = new Texture("Volume History", mScene->Instance()->Device(), camera->Framebuffer()->Extent(),
				VK_FORMAT_R32G32B32A32_SFLOAT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
			mFrameIndex = 0;
		}

		commandBuffer->TransitionBarrier(camera->Framebuffer()->ColorBuffer(0), VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(camera->Framebuffer()->ColorBuffer(1), VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(mHistoryBuffer, VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(commandBuffer->Device()->AssetManager()->NoiseTexture(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		uint2 res(camera->Framebuffer()->Extent().width, camera->Framebuffer()->Extent().height);
		uint3 vres(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);
		float4x4 ivp[2]{
			camera->InverseViewProjection(EYE_LEFT),
			camera->InverseViewProjection(EYE_RIGHT)
		};
		float3 vp[2]{
			mVolumePosition - (camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_LEFT), 1)).xyz,
			mVolumePosition - (camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_RIGHT), 1)).xyz
		};
		uint2 writeOffset(0);

		// Bake the volume if necessary
		if (mBakeDirty && mBakedVolume) {
			set<string> kw;
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
			ComputeShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeVolume", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeVolume", shader->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding);
			if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding);
			ds->CreateStorageTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Output").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "VolumeResolution", vres);
			commandBuffer->PushConstantRef(shader, "MaskValue", mMaskValue);
			commandBuffer->PushConstantRef(shader, "RemapRange", mRemapRange);
			commandBuffer->PushConstantRef(shader, "HueRange", mHueRange);
			vkCmdDispatch(*commandBuffer, (mRawVolume->Extent().width + 3) / 4, (mRawVolume->Extent().height + 3) / 4, (mRawVolume->Extent().depth + 3) / 4);

			commandBuffer->Barrier(mBakedVolume);
			mBakeDirty = false;
		}

		// Shader keywords shared by the gradient bake and the final render
		set<string> kw;
		if (!mBakedVolume) {
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
		}
		
		// Bake the gradient if necessary
		if (mGradientDirty && mGradient) {
			ComputeShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/precompute.stm")->GetCompute("BakeGradient", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeGradient", shader->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateStorageTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Volume").second.binding);
			else {
				ds->CreateStorageTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding);
				if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding);
			}
			ds->CreateStorageTextureDescriptor(mGradient, shader->mDescriptorBindings.at("Output").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "VolumeResolution", vres);
			commandBuffer->PushConstantRef(shader, "MaskValue", mMaskValue);
			commandBuffer->PushConstantRef(shader, "RemapRange", mRemapRange);
			commandBuffer->PushConstantRef(shader, "HueRange", mHueRange);
			vkCmdDispatch(*commandBuffer, (mRawVolume->Extent().width + 3) / 4, (mRawVolume->Extent().height + 3) / 4, (mRawVolume->Extent().depth + 3) / 4);

			commandBuffer->Barrier(mBakedVolume);
			mGradientDirty = false;
		}

		// Render the volume
		{
			if (mLighting) kw.emplace("LIGHTING");
			if (mGradient) kw.emplace("GRADIENT_TEXTURE");
			ComputeShader* shader = commandBuffer->Device()->AssetManager()->LoadShader("Shaders/volume.stm")->GetCompute("Render", kw);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("Draw Volume", shader->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateSampledTextureDescriptor(mBakedVolume, shader->mDescriptorBindings.at("Volume").second.binding, 0, 0, VK_IMAGE_LAYOUT_GENERAL);
			else {
				ds->CreateSampledTextureDescriptor(mRawVolume, shader->mDescriptorBindings.at("Volume").second.binding, 0, 0, VK_IMAGE_LAYOUT_GENERAL);
				if (mRawMask) ds->CreateSampledTextureDescriptor(mRawMask, shader->mDescriptorBindings.at("RawMask").second.binding, 0, 0, VK_IMAGE_LAYOUT_GENERAL);
			}
			if (mLighting && mGradient)
				ds->CreateStorageTextureDescriptor(mGradient, shader->mDescriptorBindings.at("Gradient").second.binding);
			ds->CreateStorageTextureDescriptor(mHistoryBuffer, shader->mDescriptorBindings.at("History").second.binding);
			ds->CreateStorageTextureDescriptor(camera->Framebuffer()->ColorBuffer(0), shader->mDescriptorBindings.at("RenderTarget").second.binding);
			ds->CreateStorageTextureDescriptor(camera->Framebuffer()->ColorBuffer(1), shader->mDescriptorBindings.at("DepthNormal").second.binding);
			ds->CreateSampledTextureDescriptor(commandBuffer->Device()->AssetManager()->NoiseTexture(), shader->mDescriptorBindings.at("NoiseTex").second.binding);
			ds->FlushWrites();
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->mPipelineLayout, 0, 1, *ds, 0, nullptr);

			commandBuffer->PushConstantRef(shader, "VolumeResolution", vres);
			commandBuffer->PushConstantRef(shader, "VolumeRotation", mVolumeRotation.xyzw);
			commandBuffer->PushConstantRef(shader, "VolumeScale", mVolumeScale);
			commandBuffer->PushConstantRef(shader, "InvVolumeRotation", inverse(mVolumeRotation).xyzw);
			commandBuffer->PushConstantRef(shader, "InvVolumeScale", 1.f / mVolumeScale);
			commandBuffer->PushConstantRef(shader, "Density", mDensity);
			commandBuffer->PushConstantRef(shader, "MaskValue", mMaskValue);
			commandBuffer->PushConstantRef(shader, "RemapRange", mRemapRange);
			commandBuffer->PushConstantRef(shader, "HueRange", mHueRange);
			commandBuffer->PushConstantRef(shader, "StepSize", mStepSize);
			commandBuffer->PushConstantRef(shader, "FrameIndex", mFrameIndex);

			switch (camera->StereoMode()) {
			case STEREO_NONE:
				commandBuffer->PushConstantRef(shader, "VolumePosition", vp[0]);
				commandBuffer->PushConstantRef(shader, "InvViewProj", ivp[0]);
				commandBuffer->PushConstantRef(shader, "WriteOffset", writeOffset);
				commandBuffer->PushConstantRef(shader, "ScreenResolution", res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			case STEREO_SBS_HORIZONTAL:
				res.x /= 2;
				commandBuffer->PushConstantRef(shader, "VolumePosition", vp[0]);
				commandBuffer->PushConstantRef(shader, "InvViewProj", ivp[0]);
				commandBuffer->PushConstantRef(shader, "WriteOffset", writeOffset);
				commandBuffer->PushConstantRef(shader, "ScreenResolution", res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				writeOffset.x = res.x;
				commandBuffer->PushConstantRef(shader, "VolumePosition", vp[1]);
				commandBuffer->PushConstantRef(shader, "InvViewProj", ivp[1]);
				commandBuffer->PushConstantRef(shader, "WriteOffset", writeOffset);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			case STEREO_SBS_VERTICAL:
				res.y /= 2;
				commandBuffer->PushConstantRef(shader, "VolumePosition", vp[0]);
				commandBuffer->PushConstantRef(shader, "InvViewProj", ivp[0]);
				commandBuffer->PushConstantRef(shader, "WriteOffset", writeOffset);
				commandBuffer->PushConstantRef(shader, "ScreenResolution", res);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				writeOffset.y = res.y;
				commandBuffer->PushConstantRef(shader, "VolumePosition", vp[1]);
				commandBuffer->PushConstantRef(shader, "InvViewProj", ivp[1]);
				commandBuffer->PushConstantRef(shader, "WriteOffset", writeOffset);
				vkCmdDispatch(*commandBuffer, (res.x + 7) / 8, (res.y + 7) / 8, 1);
				break;
			}
		}

		mFrameIndex++;
	}
	
	void LoadVolume(CommandBuffer* commandBuffer, const fs::path& folder, ImageStackType type) {
		
		vkDeviceWaitIdle(*mScene->Instance()->Device());

		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mBakedVolume);
		safe_delete(mGradient);

		Texture* vol = nullptr;
		switch (type) {
		case IMAGE_STACK_STANDARD:
			vol = ImageLoader::LoadStandardStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		case IMAGE_STACK_DICOM:
			vol = ImageLoader::LoadDicomStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		case IMAGE_STACK_RAW:
			vol = ImageLoader::LoadRawStack(folder, mScene->Instance()->Device(), &mVolumeScale);
			break;
		}
		
		if (!vol) {
			fprintf_color(COLOR_RED, stderr, "Failed to load volume!\n");
			return;
		}

		switch (vol->Format()) {
		default:
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SNORM:
		case VK_FORMAT_R16_USCALED:
		case VK_FORMAT_R16_SSCALED:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16_SFLOAT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_R64_UINT:
		case VK_FORMAT_R64_SINT:
		case VK_FORMAT_R64_SFLOAT:
			mRawVolumeColored = false;
			break;

		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SNORM:
		case VK_FORMAT_R16G16B16A16_USCALED:
		case VK_FORMAT_R16G16B16A16_SSCALED:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_R64G64B64A64_UINT:
		case VK_FORMAT_R64G64B64A64_SINT:
		case VK_FORMAT_R64G64B64A64_SFLOAT:
			mRawVolumeColored = true;
			break;

		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK:
		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
			break;
		}

		mVolumeRotation = quaternion(0,0,0,1);
		mVolumePosition = float3(0, 1.6f, 0);

		mRawVolume = vol;

		mRawMask = ImageLoader::LoadStandardStack(folder.string() + "/_mask", mScene->Instance()->Device(), nullptr, true, 1, false);
		
		// TODO: check device->MemoryUsage(), only create the baked volume and gradient textures if there is enough VRAM

		mBakedVolume = new Texture("Volume", mScene->Instance()->Device(), mRawVolume->Extent(), VK_FORMAT_R8G8B8A8_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		mBakeDirty = true;
	
		mGradient = new Texture("Gradient", mScene->Instance()->Device(), mRawVolume->Extent(), VK_FORMAT_R8G8B8A8_SNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
		mGradientDirty = true;

		commandBuffer->TransitionBarrier(mRawVolume, VK_IMAGE_LAYOUT_GENERAL);
		if (mRawMask) commandBuffer->TransitionBarrier(mRawMask, VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(mBakedVolume, VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(mGradient, VK_IMAGE_LAYOUT_GENERAL);

		mFrameIndex = 0;
	}
};

ENGINE_PLUGIN(DicomVis)