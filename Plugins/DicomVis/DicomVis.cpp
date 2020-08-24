#include <Core/EnginePlugin.hpp>
#include <Scene/Renderers/MeshRenderer.hpp>
#include <Util/Profiler.hpp>
#include <assimp/pbrmaterial.h>

#include "ImageLoader.hpp"
#include "Shaders/common.hlsli"

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

struct RenderVolume {
	enum ShadingMode {
		SHADING_NONE,
		SHADING_LOCAL,
	};

	bool mColorize = false;
	ShadingMode mShadingMode = SHADING_NONE;
	float mSampleRate = 0;

	// The volume loaded directly from the folder
	Texture* mRawVolume = nullptr;
	// The mask loaded directly from the folder
	Texture* mRawMask = nullptr;
	// The baked volume. This CAN be nullptr, in which case the pipeline will use the raw volume to compute colors on the fly.
	Texture* mBakedVolume = nullptr;
	bool mBakeDirty = false;
	// The gradient of the volume. This CAN be nullptr, in which case the pipeline will compute the gradient on the fly.
	Texture* mGradient = nullptr;
	bool mGradientDirty = false;
	
	Buffer* mUniformBuffer = nullptr;
	VolumeUniformBuffer* mUniforms = nullptr;

	RenderVolume(Device* device) {
		mUniformBuffer = new Buffer("Volume Uniforms", device, sizeof(VolumeUniformBuffer), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached);
		mUniforms = (VolumeUniformBuffer*)mUniformBuffer->MappedData();
		mUniforms->VolumeRotation = quaternion(0,0,0,1);
		mUniforms->InvVolumeRotation = quaternion(0,0,0,1);
		mUniforms->VolumePosition = float3(0, 1.6f, 0);
		mUniforms->VolumeScale = 1.f;
		mUniforms->InvVolumeScale = 1.f;
		mUniforms->Density = 1.f;
		mUniforms->FrameIndex = 0;
		mUniforms->MaskValue = MASK_ALL;
		mUniforms->RemapRange = float2(.01f, .5f);
		mUniforms->HueRange = float2(.125f, 1.f);
	}
	~RenderVolume() {
		safe_delete(mRawVolume);
		safe_delete(mRawMask);
		safe_delete(mBakedVolume);
		safe_delete(mGradient);
		safe_delete(mUniformBuffer);
	}
	
	void DrawGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) {
		bool localShading = mShadingMode == SHADING_LOCAL;
		gui->LayoutTitle("Render Settings");
		gui->LayoutSlider("Sample Rate", mSampleRate, .01f, 1);
		gui->LayoutSlider("Density Scale", mUniforms->Density, 0, 100);
		if (gui->LayoutRangeSlider("Remap Density", mUniforms->RemapRange, 0, 1)) mBakeDirty = true;
		gui->LayoutToggle("Local Shading", localShading); 
		gui->LayoutToggle("Density to Hue", mColorize);
		if (mColorize && gui->LayoutRangeSlider("Hue Range", mUniforms->HueRange, 0, 1)) mBakeDirty = true;

		mShadingMode = localShading ? SHADING_LOCAL : SHADING_NONE;
	}

	void UpdateBake(CommandBuffer* commandBuffer) {
		if (!mRawVolume) return;

		set<string> keywords;
		if (mRawMask) keywords.emplace("MASK");
		if (ChannelCount(mRawVolume->Format()) == 1) {
			keywords.emplace("SINGLE_CHANNEL");
			if (mColorize) keywords.emplace("COLORIZE");
		}
		
		// Bake the volume if necessary
		if (mBakeDirty && mBakedVolume) {
			ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/precompute.stmb")->GetCompute("BakeVolume", keywords);
			commandBuffer->BindPipeline(pipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeVolume", pipeline->mDescriptorSetLayouts[0]);
			ds->CreateTextureDescriptor("Volume", mRawVolume, pipeline);
			if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
			ds->CreateTextureDescriptor("Output", mBakedVolume, pipeline);
			ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
			commandBuffer->BindDescriptorSet(ds, 0);

			commandBuffer->DispatchAligned(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

			commandBuffer->Barrier(mBakedVolume);
			mBakeDirty = false;
		}

		// Bake the gradient if necessary
		if (mGradientDirty && mGradient) {
			ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/precompute.stmb")->GetCompute("BakeGradient", keywords);
			commandBuffer->BindPipeline(pipeline);

			DescriptorSet* ds = commandBuffer->GetDescriptorSet("BakeGradient", pipeline->mDescriptorSetLayouts[0]);
			if (mBakedVolume)
				ds->CreateTextureDescriptor("Volume", mBakedVolume, pipeline);
			else {
				ds->CreateTextureDescriptor("Volume", mRawVolume, pipeline);
				if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
			}
			ds->CreateTextureDescriptor("Output", mGradient, pipeline);
			ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
			commandBuffer->BindDescriptorSet(ds, 0);

			commandBuffer->DispatchAligned(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

			commandBuffer->Barrier(mBakedVolume);
			mGradientDirty = false;
		}
	}

	void Draw(CommandBuffer* commandBuffer, Framebuffer* framebuffer, Camera* camera) {
		if (!mRawVolume && !mBakedVolume) return;

		mUniforms->FrameIndex = (uint32_t)(commandBuffer->Device()->FrameCount() % 0x00000000FFFFFFFFull);

		set<string> keywords;
		if (mRawMask) keywords.emplace("MASK");
		if (mBakedVolume) keywords.emplace("BAKED");
		else if (ChannelCount(mRawVolume->Format()) == 1) {
			keywords.emplace("SINGLE_CHANNEL");
			if (mColorize) keywords.emplace("COLORIZE");
		}
		switch (mShadingMode) {
			case SHADING_LOCAL:
				keywords.emplace("SHADING_LOCAL");
				break;
		}
		if (mGradient) keywords.emplace("GRADIENT_TEXTURE");
	
		uint2 res(framebuffer->Extent().width, framebuffer->Extent().height);
		float3 camPos[2] {
			(camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(StereoEye::eLeft), 1)).xyz,
			(camera->ObjectToWorld() * float4(camera->EyeOffsetTranslate(StereoEye::eRight), 1)).xyz
		};


		ComputePipeline* pipeline = commandBuffer->Device()->AssetManager()->LoadPipeline("Shaders/volume.stmb")->GetCompute("Render", keywords);
		commandBuffer->BindPipeline(pipeline);

		DescriptorSet* ds = commandBuffer->GetDescriptorSet("Draw Volume", pipeline->mDescriptorSetLayouts[0]);
		ds->CreateTextureDescriptor("RenderTarget", framebuffer->Attachment("stm_main_resolve"), pipeline);
		ds->CreateTextureDescriptor("DepthBuffer", framebuffer->Attachment("stm_main_depth"), pipeline, 0, vk::ImageLayout::eShaderReadOnlyOptimal);
		ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
		ds->CreateTextureDescriptor("Volume", mBakedVolume ? mBakedVolume : mRawVolume, pipeline);
		if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
		if (mGradient) ds->CreateTextureDescriptor("Gradient", mGradient, pipeline);
		commandBuffer->BindDescriptorSet(ds, 0);
	
		commandBuffer->PushConstantRef("CameraPosition", camPos[0]);
		commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(StereoEye::eLeft));
		commandBuffer->PushConstantRef("WriteOffset", uint2(0, 0));
		commandBuffer->PushConstantRef("SampleRate", mSampleRate);

		switch (camera->StereoMode()) {
		case StereoMode::eNone:
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			break;
		case StereoMode::eHorizontal:
			res.x /= 2;
			// left eye
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			// right eye
			commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(StereoEye::eRight));
			commandBuffer->PushConstantRef("CameraPosition", camPos[1]);
			commandBuffer->PushConstantRef("WriteOffset", uint2(res.x, 0));
			commandBuffer->DispatchAligned(res);
			break;
		case StereoMode::eVertical:
			res.y /= 2;
			// left eye
			commandBuffer->PushConstantRef("ScreenResolution", res);
			commandBuffer->DispatchAligned(res);
			// right eye
			commandBuffer->PushConstantRef("InvViewProj", camera->InverseViewProjection(StereoEye::eRight));
			commandBuffer->PushConstantRef("CameraPosition", camPos[1]);
			commandBuffer->PushConstantRef("WriteOffset", uint2(0, res.y));
			commandBuffer->DispatchAligned(res);
			break;
		}
	}
};

RenderVolume* LoadVolume(CommandBuffer* commandBuffer, const fs::path& folder, ImageStackType type) {
	RenderVolume* v = new RenderVolume(commandBuffer->Device());

	switch (type) {
	case IMAGE_STACK_STANDARD:
		v->mRawVolume = ImageLoader::LoadStandardStack(folder, commandBuffer->Device(), &v->mUniforms->VolumeScale);
		break;
	case IMAGE_STACK_DICOM:
		v->mRawVolume = ImageLoader::LoadDicomStack(folder, commandBuffer->Device(), &v->mUniforms->VolumeScale);
		break;
	case IMAGE_STACK_RAW:
		v->mRawVolume = ImageLoader::LoadRawStack(folder, commandBuffer->Device(), &v->mUniforms->VolumeScale);
		break;
	}
	
	if (!v->mRawVolume) {
		fprintf_color(ConsoleColorBits::eRed, stderr, "Failed to load volume!\n");
		delete v;
		return nullptr;
	}

	v->mUniforms->InvVolumeScale = 1 / v->mUniforms->VolumeScale;
	v->mUniforms->VolumeResolution = { v->mRawVolume->Extent().width, v->mRawVolume->Extent().height, v->mRawVolume->Extent().depth };

	v->mRawMask = ImageLoader::LoadStandardStack(folder.string() + "/_mask", commandBuffer->Device(), nullptr, true, 1, false);

	// TODO: check device->MemoryUsage(), only create the baked volume and gradient textures if there is enough VRAM

	//mBakedVolume = new Texture("Baked Volume", commandBuffer->Device(), mRawVolume->Extent(), vk::Format::eR8G8B8A8Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mBakeDirty = true;

	//mGradient = new Texture("Gradient", commandBuffer->Device(), mRawVolume->Extent(), vk::Format::eR8G8B8A8Snorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mGradientDirty = true;
	return v;
}

class DicomVis : public EnginePlugin {
private:
	Scene* mScene = nullptr;
	MouseKeyboardInput* mKeyboardInput = nullptr;
	Camera* mMainCamera = nullptr;

	RenderVolume* mVolume = nullptr;

	float mZoom = 0;
	bool mShowPerformance = false;

	std::thread mScanThread;
	bool mScanDone = false;

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
			fprintf_color(ConsoleColorBits::eRed, stderr, "DicomVis: Could not locate datapath. Please specify with --datapath <path>\n");
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
	PLUGIN_EXPORT DicomVis() {}
	PLUGIN_EXPORT ~DicomVis() {
		if (mScanThread.joinable()) mScanThread.join();
		safe_delete(mVolume);
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
		mScene->SetAttachmentInfo("stm_main_resolve", info.first, info.second | vk::ImageUsageFlagBits::eStorage);

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
				if (mVolume) mVolume->mUniforms->FrameIndex = 0;
			}
			if (mVolume && mKeyboardInput->KeyDown(MOUSE_LEFT)) {
				float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mKeyboardInput->CursorDelta().x - mMainCamera->WorldRotation() * float3(1, 0, 0) * mKeyboardInput->CursorDelta().y;
				if (dot(axis, axis) > .001f) {
					mVolume->mUniforms->VolumeRotation = quaternion(length(axis) * .003f, -normalize(axis)) * mVolume->mUniforms->VolumeRotation;
					mVolume->mUniforms->InvVolumeRotation = inverse(mVolume->mUniforms->VolumeRotation);
					mVolume->mUniforms->FrameIndex = 0;
				}
			}
		}
	}
	PLUGIN_EXPORT void OnLateUpdate(CommandBuffer* commandBuffer) override { if (mVolume) mVolume->UpdateBake(commandBuffer); }
	
	PLUGIN_EXPORT void OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui) override {		
		bool worldSpace = camera->StereoMode() != StereoMode::eNone;

		// Draw performance overlay
		#ifdef PROFILER_ENABLE
		if (mShowPerformance && !worldSpace) Profiler::DrawGui(gui, (uint32_t)mScene->FPS());
		#endif

		if (!mScanDone) return;
		if (mScanThread.joinable()) mScanThread.join();

		if (worldSpace)
			gui->BeginWorldLayout(LayoutAxis::eVertical, float4x4::TRS(float3(-.85f, 1, 0), quaternion(0, 0, 0, 1), .001f), fRect2D(0, 0, 300, 850));
		else
			gui->BeginScreenLayout(LayoutAxis::eVertical, fRect2D(10, (float)mScene->Instance()->Window()->SwapchainExtent().height - 450 - 10, 300, 450));


		gui->LayoutTitle("Load Dataset");
		gui->LayoutSeparator();
		float prev = gui->mLayoutTheme.mControlSize;
		gui->mLayoutTheme.mControlSize = 24;
		gui->BeginScrollSubLayout(175, mDataFolders.size() * (gui->mLayoutTheme.mControlSize + 2*gui->mLayoutTheme.mControlPadding));
		
		for (const auto& p : mDataFolders)
			if (gui->LayoutTextButton(fs::path(p.first).stem().string(), TextAnchor::eMin)) {
				commandBuffer->Device()->Flush();
				safe_delete(mVolume);
				mVolume = LoadVolume(commandBuffer, p.first, p.second);
			}

		gui->mLayoutTheme.mControlSize = prev;
		gui->EndLayout();

		if (mVolume) mVolume->DrawGui(commandBuffer, camera, gui);

		gui->EndLayout();
	}

	PLUGIN_EXPORT void OnPostProcess(CommandBuffer* commandBuffer, Framebuffer* framebuffer, const set<Camera*>& cameras) override {
		if (!mVolume) return;
		for (Camera* camera : cameras) mVolume->Draw(commandBuffer, framebuffer, camera);
	}
};

ENGINE_PLUGIN(DicomVis)