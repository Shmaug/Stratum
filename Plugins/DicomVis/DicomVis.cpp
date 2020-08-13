#include <Data/AssetManager.hpp>
#include <Data/Font.hpp>
#include <Input/InputManager.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshRenderer.hpp>
#include <Scene/GuiContext.hpp>
#include <Util/Profiler.hpp>

#include <Core/EnginePlugin.hpp>
#include <assimp/pbrmaterial.h>

#include "ImageLoader.hpp"
#include "Shaders/common.h"

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
	Camera* mMainCamera;

	bool mLighting;
	bool mColorize;
	Buffer* mVolumeUniformBuffer;
	VolumeUniformBuffer* mVolumeUniforms;

	// The volume loaded directly from the folder
	Texture* mRawVolume;
	// The mask loaded directly from the folder
	Texture* mRawMask;
	// The baked volume. This CAN be nullptr, in which case the pipeline will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume;
	// The gradient of the volume. This CAN be nullptr, in which case the pipeline will compute the gradient on the fly.
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

	void ScanFolders() {
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
	void LoadVolume(CommandBuffer* commandBuffer, const fs::path& folder, ImageStackType type) {
		vkDeviceWaitIdle(*mScene->Instance()->Device());

		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mBakedVolume);
		safe_delete(mGradient);

		Texture* vol = nullptr;
		switch (type) {
		case IMAGE_STACK_STANDARD:
			vol = ImageLoader::LoadStandardStack(folder, mScene->Instance()->Device(), &mVolumeUniforms->VolumeScale);
			break;
		case IMAGE_STACK_DICOM:
			vol = ImageLoader::LoadDicomStack(folder, mScene->Instance()->Device(), &mVolumeUniforms->VolumeScale);
			break;
		case IMAGE_STACK_RAW:
			vol = ImageLoader::LoadRawStack(folder, mScene->Instance()->Device(), &mVolumeUniforms->VolumeScale);
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

		mVolumeUniforms->VolumeRotation = quaternion(0,0,0,1);
		mVolumeUniforms->InvVolumeRotation = quaternion(0,0,0,1);
		mVolumeUniforms->VolumePosition = float3(0, 1.6f, 0);
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

		mVolumeUniforms->FrameIndex = 0;
	}
	void DrawVolume(CommandBuffer* commandBuffer, Framebuffer* framebuffer, Camera* camera) {
		Texture* noiseTexture = commandBuffer->Device()->AssetManager()->NoiseTexture();

		commandBuffer->TransitionBarrier(mScene->GetAttachment("stm_main_resolve"), VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->TransitionBarrier(mScene->GetAttachment("stm_main_depth"), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		commandBuffer->TransitionBarrier(noiseTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		set<string> kw;
		if (!mBakedVolume) {
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
		}
	
		uint2 res(framebuffer->Extent().width, framebuffer->Extent().height);
		float3 camPos[2] {
			(camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_LEFT), 1)).xyz,
			(camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(EYE_RIGHT), 1)).xyz
		};

		if (mLighting) kw.emplace("LIGHTING");
		if (mGradient) kw.emplace("GRADIENT_TEXTURE");
		ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/volume.stmb")->GetCompute("Render", kw);
		commandBuffer->BindPipeline(pipeline);

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Draw Volume", pipeline->mDescriptorSetLayouts[0]);
		ds->CreateStorageTextureDescriptor(mScene->GetAttachment("stm_main_resolve"), pipeline->GetDescriptorLocation("RenderTarget"));
		ds->CreateSampledTextureDescriptor(mScene->GetAttachment("stm_main_depth"), pipeline->GetDescriptorLocation("DepthBuffer"));
		ds->CreateUniformBufferDescriptor(mVolumeUniformBuffer, sizeof(VolumeUniformBuffer), pipeline->GetDescriptorLocation("VolumeUniforms"));
		ds->CreateSampledTextureDescriptor(noiseTexture, pipeline->GetDescriptorLocation("NoiseTex"));
		if (mBakedVolume)
			ds->CreateSampledTextureDescriptor(mBakedVolume, pipeline->GetDescriptorLocation("Volume"), 0, 0, VK_IMAGE_LAYOUT_GENERAL);
		else {
			ds->CreateSampledTextureDescriptor(mRawVolume, pipeline->GetDescriptorLocation("Volume"), 0, 0, VK_IMAGE_LAYOUT_GENERAL);
			if (mRawMask) ds->CreateSampledTextureDescriptor(mRawMask, pipeline->GetDescriptorLocation("RawMask"), 0, 0, VK_IMAGE_LAYOUT_GENERAL);
		}
		if (mLighting && mGradient)
			ds->CreateStorageTextureDescriptor(mGradient, pipeline->GetDescriptorLocation("Gradient"));
		commandBuffer->BindDescriptorSet(ds, 0);
	
		commandBuffer->PushConstantRef("CameraPosition", camPos[0]);
		commandBuffer->PushConstantRef("CameraNear", camera->Near());
		commandBuffer->PushConstantRef("CameraFar", camera->Far());
		commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(EYE_LEFT));
		commandBuffer->PushConstantRef("WriteOffset", uint2(0, 0));

		switch (camera->StereoMode()) {
		case STEREO_NONE:
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			break;
		case STEREO_SBS_HORIZONTAL:
			res.x /= 2;
			// left eye
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			// right eye
			commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(EYE_RIGHT));
			commandBuffer->PushConstantRef("CameraPosition", camPos[1]);
			commandBuffer->PushConstantRef("WriteOffset", uint2(res.x, 0));
			commandBuffer->DispatchAligned(res);
			break;
		case STEREO_SBS_VERTICAL:
			res.y /= 2;
			// left eye
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			// right eye
			commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(EYE_RIGHT));
			commandBuffer->PushConstantRef("CameraPosition", camPos[1]);
			commandBuffer->PushConstantRef("WriteOffset", uint2(0, res.y));
			commandBuffer->DispatchAligned(res);
			break;
		}
	}

public:
	PLUGIN_EXPORT DicomVis() : mScene(nullptr), mShowPerformance(false), mRawVolume(nullptr), mRawMask(nullptr), mBakedVolume(nullptr),
		mGradient(nullptr), mBakeDirty(false), mGradientDirty(false), mColorize(false), mLighting(false), mHistoryBuffer(nullptr), mVolumeUniformBuffer(nullptr) {}
	PLUGIN_EXPORT ~DicomVis() {
		if (mScanThread.joinable()) mScanThread.join();
		safe_delete(mVolumeUniformBuffer);
		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mGradient);
		safe_delete(mBakedVolume);
	}

protected:
	PLUGIN_EXPORT bool OnSceneInit(Scene* scene) override {
		mScene = scene;
		mKeyboardInput = mScene->Instance()->InputManager()->GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;
		
		mMainCamera = mScene->CreateObject<Camera>("Camera", set<RenderTargetIdentifier> { "stm_main_render", "stm_main_resolve" "stm_main_depth" });
		mMainCamera->Near(.00625f);
		mMainCamera->Far(1024.f);
		mMainCamera->FieldOfView(radians(65.f));
		mMainCamera->LocalPosition(0, 1.6f, -mZoom);

		mScene->AmbientLight(.5f);

		auto info = mScene->GetAttachmentInfo("stm_main_resolve");
		mScene->SetAttachmentInfo("stm_main_resolve", info.first, info.second | VK_IMAGE_USAGE_STORAGE_BIT);

		mScanDone = false;
		mScanThread = thread(&DicomVis::ScanFolders, this);
	
		return true;
	}
	PLUGIN_EXPORT void OnUpdate(CommandBuffer* commandBuffer) override {
		if (mKeyboardInput->KeyDownFirst(KEY_TILDE)) mShowPerformance = !mShowPerformance;

		if (mKeyboardInput->GetPointerLast(0)->mGuiHitT < 0) {
			if (mKeyboardInput->ScrollDelta() != 0) {
				mZoom = clamp(mZoom - mKeyboardInput->ScrollDelta() * .025f, -1.f, 5.f);
				mMainCamera->LocalPosition(0, 1.6f, -mZoom);
				mVolumeUniforms->FrameIndex = 0;
			}
			if (mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x - mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f){
					mVolumeUniforms->VolumeRotation = quaternion(length(axis) * .003f, -normalize(axis)) * mVolumeUniforms->VolumeRotation;
					mVolumeUniforms->InvVolumeRotation = inverse(mVolumeUniforms->VolumeRotation);
					mVolumeUniforms->FrameIndex = 0;
				}
			}
		}
	}
	PLUGIN_EXPORT void OnLateUpdate(CommandBuffer* commandBuffer) override {
		if (!mRawVolume) return;

		if (!mVolumeUniformBuffer) {
			mVolumeUniformBuffer = new Buffer("Volume Uniforms", commandBuffer->Device(), sizeof(VolumeUniformBuffer), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
			mVolumeUniforms = (VolumeUniformBuffer*)mVolumeUniformBuffer->MappedData();
			mVolumeUniforms->VolumeRotation = quaternion(0,0,0,1);
			mVolumeUniforms->InvVolumeRotation = quaternion(0,0,0,1);
			mVolumeUniforms->VolumeScale = 1.f;
			mVolumeUniforms->InvVolumeScale = 1.f;
			mVolumeUniforms->Density = 500.f;
			mVolumeUniforms->StepSize = .001f;
			mVolumeUniforms->FrameIndex = 0;
			mVolumeUniforms->MaskValue = MASK_ALL;
			mVolumeUniforms->RemapRange = float2(.01f, .5f);
			mVolumeUniforms->HueRange = float2(.125f, 1.f);
			mVolumeUniforms->VolumePosition = float3(0, 1.6f, 0);
		}

		commandBuffer->TransitionBarrier(commandBuffer->Device()->AssetManager()->NoiseTexture(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		// Bake the volume if necessary
		if (mBakeDirty && mBakedVolume) {
			set<string> kw;
			if (mRawMask) kw.emplace("MASK_COLOR");
			if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
			else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
			else kw.emplace("NON_BAKED_R");
			ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/precompute.stmb")->GetCompute("BakeVolume", kw);
			commandBuffer->BindPipeline(pipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeVolume", pipeline->mDescriptorSetLayouts[0]);
			ds->CreateStorageTextureDescriptor(mRawVolume, pipeline->GetDescriptorLocation("Volume"));
			if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, pipeline->GetDescriptorLocation("RawMask"));
			ds->CreateStorageTextureDescriptor(mBakedVolume, pipeline->GetDescriptorLocation("Output"));
			ds->CreateUniformBufferDescriptor(mVolumeUniformBuffer, pipeline->GetDescriptorLocation("VolumeUniforms"));
			commandBuffer->BindDescriptorSet(ds, 0);

			commandBuffer->DispatchAligned(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

			commandBuffer->Barrier(mBakedVolume);
			mBakeDirty = false;
		}

		// Bake the gradient if necessary
		if (mGradientDirty && mGradient) {
			set<string> kw;
			if (!mBakedVolume) {
				if (mRawMask) kw.emplace("MASK_COLOR");
				if (mRawVolumeColored) kw.emplace("NON_BAKED_RGBA");
				else if (mColorize) kw.emplace("NON_BAKED_R_COLORIZE");
				else kw.emplace("NON_BAKED_R");
			}
			ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/precompute.stmb")->GetCompute("BakeGradient", kw);
			commandBuffer->BindPipeline(pipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeGradient", pipeline->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateStorageTextureDescriptor(mBakedVolume, pipeline->GetDescriptorLocation("Volume"));
			else {
				ds->CreateStorageTextureDescriptor(mRawVolume, pipeline->GetDescriptorLocation("Volume"));
				if (mRawMask) ds->CreateStorageTextureDescriptor(mRawMask, pipeline->GetDescriptorLocation("RawMask"));
			}
			ds->CreateStorageTextureDescriptor(mGradient, pipeline->GetDescriptorLocation("Output"));
			ds->CreateUniformBufferDescriptor(mVolumeUniformBuffer, pipeline->GetDescriptorLocation("VolumeUniforms"));
			commandBuffer->BindDescriptorSet(ds, 0);

			commandBuffer->DispatchAligned(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

			commandBuffer->Barrier(mBakedVolume);
			mGradientDirty = false;
		}
		
		mVolumeUniforms->FrameIndex++;
		
		mVolumeUniforms->InvVolumeRotation = inverse(mVolumeUniforms->VolumeRotation);
		mVolumeUniforms->InvVolumeScale = 1.f / mVolumeUniforms->VolumeScale;
	}
	
	PLUGIN_EXPORT void OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) override {		
		bool worldSpace = camera->StereoMode() != STEREO_NONE;

		// Draw performance overlay
		#ifdef PROFILER_ENABLE
		if (mShowPerformance && !worldSpace)
			Profiler::DrawGui(gui, (uint32_t)mScene->FPS());
		#endif

		if (!mScanDone) return;
		if (mScanThread.joinable()) mScanThread.join();

		if (worldSpace)
			gui->BeginWorldLayout(LAYOUT_VERTICAL, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 300, 850));
		else
			gui->BeginScreenLayout(LAYOUT_VERTICAL, fRect2D(10, (float)mScene->Instance()->Window()->SwapchainExtent().height - 450 - 10, 300, 450));


		gui->LayoutTitle("Load Dataset");
		gui->LayoutSeparator();
		float prev = gui->mLayoutTheme.mControlSize;
		gui->mLayoutTheme.mControlSize = 24;
		
		gui->BeginScrollSubLayout(175, mDataFolders.size() * (gui->mLayoutTheme.mControlSize + 2*gui->mLayoutTheme.mControlPadding));
		for (const auto& p : mDataFolders)
			if (gui->LayoutTextButton(fs::path(p.first).stem().string(), TEXT_ANCHOR_MIN))
				LoadVolume(commandBuffer, p.first, p.second);
		gui->mLayoutTheme.mControlSize = prev;
		gui->EndLayout();

		gui->LayoutTitle("Render Settings");
		if (gui->LayoutToggle("Colorize", mColorize)) mVolumeUniforms->FrameIndex = 0;
		if (gui->LayoutToggle("Lighting", mLighting)) mVolumeUniforms->FrameIndex = 0;
		if (gui->LayoutSlider("Step Size", mVolumeUniforms->StepSize, .0001f, .01f)) mVolumeUniforms->FrameIndex = 0;
		if (gui->LayoutSlider("Density", mVolumeUniforms->Density, 10, 50000.f)) mVolumeUniforms->FrameIndex = 0;
		if (gui->LayoutRangeSlider("Remap", mVolumeUniforms->RemapRange, 0, 1)) { mVolumeUniforms->FrameIndex = 0; mBakeDirty = true; }
		if (mColorize && gui->LayoutRangeSlider("Hue Range", mVolumeUniforms->HueRange, 0, 1)) { mVolumeUniforms->FrameIndex = 0; mBakeDirty = true; }

		gui->EndLayout();
	}

	PLUGIN_EXPORT void OnPostProcess(CommandBuffer* commandBuffer, Framebuffer* framebuffer, const set<Camera*>& cameras) override {
		if (!mRawVolume) return;
		for (Camera* camera : cameras)
			DrawVolume(commandBuffer, framebuffer, camera);
	}
};

ENGINE_PLUGIN(DicomVis)