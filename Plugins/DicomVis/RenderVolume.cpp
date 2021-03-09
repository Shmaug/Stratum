#include "RenderVolume.hpp"


using namespace dcmvs;

RenderVolume::RenderVolume(const string& name, Scene* scene, Device& device, const fs::path& imageStackFolder) : Object(mName, scene) {
  mPrecomputePipeline = device->LoadAsset<Pipeline>("Assets/Shaders/precompute.stmb", "precompute");
  mRenderPipeline = device->LoadAsset<Pipeline>("Assets/Shaders/volume.stmb", "volume");

  ImageStackType type = ImageLoader::FolderStackType(imageStackFolder);
  Vector3f stackScale;
	switch (type) {
	case ImageStackType::eStandard:
		mRawVolume = ImageLoader::LoadStandardStack(imageStackFolder, device, &stackScale);
		break;
	case ImageStackType::eDicom:
		mRawVolume = ImageLoader::LoadDicomStack(imageStackFolder, device, &stackScale);
		break;
	case ImageStackType::eRaw:
		mRawVolume = ImageLoader::LoadRawStack(imageStackFolder, device, &stackScale);
		break;
	}
	if (!mRawVolume) throw invalid_argument("could not load volume from " + name);

  LocalScale(stackScale);

	mRawMask = ImageLoader::LoadStandardStack(imageStackFolder.string() + "/_mask", device, nullptr, true, 1, false);

	// TODO: check device->MemoryUsage(), only create the baked volume and gradient textures if there is enough VRAM

	//mBakedVolume = new Texture("Baked Volume", device, mRawVolume->Extent(), vk::Format::eR8G8B8A8Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mBakeDirty = true;

	//mGradient = new Texture("Gradient", device, mRawVolume->Extent(), vk::Format::eR8G8B8A8Snorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mGradientDirty = true;

  mUniformBuffer = make_shared<Buffer>("Volume Uniforms", device, sizeof(VolumeUniformBuffer), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

void RenderVolume::DrawGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui) {
  bool localShading = mShadingMode == ShadingMode::eLocal;
  gui.LayoutTitle("Render Settings");
  gui.LayoutSlider("Sample Rate", mSampleRate, .01f, 1);
  gui.LayoutSlider("Density Scale", mDensityScale, 0, 100);
  if (gui.LayoutRangeSlider("Remap Density", mRemapRange, 0, 1)) mBakeDirty = true;
  gui.LayoutToggle("Local Shading", localShading); 
  gui.LayoutToggle("Density to Hue", mColorize);
  if (mColorize && gui.LayoutRangeSlider("Hue Range", mHueRange, 0, 1)) mBakeDirty = true;

  mShadingMode = localShading ? ShadingMode::eLocal : ShadingMode::eNone;
}

void RenderVolume::BakeRender(CommandBuffer& commandBuffer) {
  if (!mRawVolume || (!mBakeDirty && !mGradientDirty)) return;

  VolumeUniformBuffer* uniforms = reinterpret_cast<VolumeUniformBuffer*)mUniformBuffer->data();
  uniforms->VolumeRotation = WorldRotation();
  uniforms->InvVolumeRotation = inverse(uniforms->VolumeRotation);
  uniforms->VolumeScale = WorldScale();
  uniforms->Density = mDensityScale;
  uniforms->InvVolumeScale = 1.f / uniforms->InvVolumeScale;
  uniforms->MaskValue = (uint32_t)mOrganMask;
  uniforms->VolumePosition = WorldPosition();
  uniforms->FrameIndex = (uint32_t)(commandBuffer.mDevice.FrameCount() % 0x00000000FFFFFFFFull);
  uniforms->RemapRange = mRemapRange;
  uniforms->HueRange = mHueRange;
	uniforms->VolumeResolution = { mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth };

  unordered_set<string> defines;
  if (mRawMask) defines.emplace("MASK");
  if (ChannelCount(mRawVolume->Format()) == 1) {
    defines.emplace("SINGLE_CHANNEL");
    if (mColorize) defines.emplace("COLORIZE");
  }

  
  // Bake the volume if necessary
  if (mBakeDirty && mBakedVolume) {
    ComputePipeline* pipeline = mPrecomputePipeline->GetCompute("BakeVolume", defines);
    commandBuffer.BindPipeline(pipeline);

    auto ds = commandBuffer.GetDescriptorSet("BakeVolume", pipeline->mDescriptorSetLayouts[0]);
    ds->CreateTextureDescriptor("Volume", mRawVolume, pipeline);
    if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
    ds->CreateTextureDescriptor("Output", mBakedVolume, pipeline);
    ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
    commandBuffer.BindDescriptorSet(ds, 0);

    commandBuffer.DispatchTiled(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

    commandBuffer.Barrier(*mBakedVolume);
    mBakeDirty = false;
  }

  // Bake the gradient if necessary
  if (mGradientDirty && mGradient) {
    ComputePipeline* pipeline = mPrecomputePipeline->GetCompute("BakeGradient", defines);
    commandBuffer.BindPipeline(pipeline);

    auto ds = commandBuffer.GetDescriptorSet("BakeGradient", pipeline->mDescriptorSetLayouts[0]);
    if (mBakedVolume)
      ds->CreateTextureDescriptor("Volume", mBakedVolume, pipeline);
    else {
      ds->CreateTextureDescriptor("Volume", mRawVolume, pipeline);
      if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
    }
    ds->CreateTextureDescriptor("Output", mGradient, pipeline);
    ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
    commandBuffer.BindDescriptorSet(ds, 0);

    commandBuffer.DispatchTiled(mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth);

    commandBuffer.Barrier(*mBakedVolume);
    mGradientDirty = false;
  }
}

void RenderVolume::Draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, Camera& camera) {
  if (!mRawVolume && !mBakedVolume) return;

  VolumeUniformBuffer* uniforms = (VolumeUniformBuffer*)mUniformBuffer->data();
  uniforms->VolumeRotation = WorldRotation();
  uniforms->InvVolumeRotation = inverse(uniforms->VolumeRotation);
  uniforms->VolumeScale = WorldScale();
  uniforms->Density = mDensityScale;
  uniforms->InvVolumeScale = 1.f / uniforms->VolumeScale;
  uniforms->MaskValue = (uint32_t)mOrganMask;
  uniforms->VolumePosition = WorldPosition();
  uniforms->FrameIndex = (uint32_t)(commandBuffer.mDevice.FrameCount() % 0x00000000FFFFFFFFull);
  uniforms->RemapRange = mRemapRange;
  uniforms->HueRange = mHueRange;
	uniforms->VolumeResolution = { mRawVolume->Extent().width, mRawVolume->Extent().height, mRawVolume->Extent().depth };

  unordered_set<string> defines;
  if (mRawMask) defines.emplace("MASK");
  if (mBakedVolume) defines.emplace("BAKED");
  else if (ChannelCount(mRawVolume->Format()) == 1) {
    defines.emplace("SINGLE_CHANNEL");
    if (mColorize) defines.emplace("COLORIZE");
  }
  switch (mShadingMode) {
    case ShadingMode::eLocal:
      defines.emplace("SHADING_LOCAL");
      break;
  }
  if (mGradient) defines.emplace("GRADIENT_TEXTURE");

  Vector3f camPos[2] {
    (camera.Transform() * Vector4f(camera.EyeOffsetTranslate(StereoEye::eLeft), 1)).xyz,
    (camera.Transform() * Vector4f(camera.EyeOffsetTranslate(StereoEye::eRight), 1)).xyz
  };


  ComputePipeline* pipeline = mRenderPipeline->GetCompute("Render", defines);
  commandBuffer.BindPipeline(pipeline);
  auto renderTarget = framebuffer->Attachment("stm_main_resolve");

  auto ds = commandBuffer.GetDescriptorSet("Draw Volume", pipeline->mDescriptorSetLayouts[0]);
  ds->CreateTextureDescriptor("RenderTarget", renderTarget, pipeline);
  ds->CreateTextureDescriptor("DepthBuffer", framebuffer->Attachment("stm_main_depth"), pipeline, 0, vk::ImageLayout::eShaderReadOnlyOptimal);
  ds->CreateBufferDescriptor("VolumeUniforms", mUniformBuffer, pipeline);
  ds->CreateTextureDescriptor("Volume", mBakedVolume ? mBakedVolume : mRawVolume, pipeline);
  if (mRawMask) ds->CreateTextureDescriptor("RawMask", mRawMask, pipeline);
  if (mGradient) ds->CreateTextureDescriptor("Gradient", mGradient, pipeline);
  commandBuffer.BindDescriptorSet(ds, 0);

  commandBuffer.PushConstantRef("CameraPosition", camPos[0]);
  commandBuffer.PushConstantRef("InvViewProj", camera.InverseViewProjection(StereoEye::eLeft));
  commandBuffer.PushConstantRef("WriteOffset", Vector2i(0, 0));
  commandBuffer.PushConstantRef("SampleRate", mSampleRate);

  Vector2i res(renderTarget->Extent().width, renderTarget->Extent().height);
  switch (camera.StereoMode()) {
  case StereoMode::eNone:
    commandBuffer.PushConstantRef("ScreenResolution", res);
    commandBuffer.DispatchTiled(res);
    break;
  case StereoMode::eHorizontal:
    res.x /= 2;
    // left eye
    commandBuffer.PushConstantRef("ScreenResolution", res);
    commandBuffer.DispatchTiled(res);
    // right eye
    commandBuffer.PushConstantRef("InvViewProj", camera.InverseViewProjection(StereoEye::eRight));
    commandBuffer.PushConstantRef("CameraPosition", camPos[1]);
    commandBuffer.PushConstantRef("WriteOffset", Vector2i(res.x, 0));
    commandBuffer.DispatchTiled(res);
    break;
  case StereoMode::eVertical:
    res.y /= 2;
    // left eye
    commandBuffer.PushConstantRef("ScreenResolution", res);
    commandBuffer.DispatchTiled(res);
    // right eye
    commandBuffer.PushConstantRef("InvViewProj", camera.InverseViewProjection(StereoEye::eRight));
    commandBuffer.PushConstantRef("CameraPosition", camPos[1]);
    commandBuffer.PushConstantRef("WriteOffset", Vector2i(0, res.y));
    commandBuffer.DispatchTiled(res);
    break;
  }
}