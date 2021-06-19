#include "VolumeRenderer.hpp"

using namespace dcmvs;

VolumeRenderer::VolumeRenderer(NodeGraph::Node& node, const string& name, Device& device, const fs::path& imageStackFolder) : mNode(node) {
  mBakeColorMaterial = make_shared<Material>("Assets/Shaders/precompute.stmb", "precompute");
  mBakeGradientMaterial = make_shared<Material>("Assets/Shaders/precompute.stmb", "precompute");
  mRenderMaterial = make_shared<Material>("Assets/Shaders/volume.stmb", "volume");

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

	//mBakedVolume = new Texture("Baked Volume", device, mRawVolume->extent(), vk::Format::eR8G8B8A8Unorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mBakeDirty = true;

	//mGradient = new Texture("Gradient", device, mRawVolume->extent(), vk::Format::eR8G8B8A8Snorm, 1, vk::SampleCountFlagBits::e1, vk::ImageUsageFlagBits::eStorage);
	//mGradientDirty = true;
}

void VolumeRenderer::imgui() {
  bool localShading = mShadingMode == ShadingMode::eLocal;
  
  ImGui::Begin("Render Settings");
  ImGui::SliderFloat("Sample Rate", &mSampleRate, .01f, 1);
  ImGui::SliderFloat("Density Scale", &mDensityScale, 0, 100);
  if (ImGui::DragFloat2("Remap Density", mRemapRange.data(), 1, 0, 1)) mBakeDirty = true;
  ImGui::Checkbox("Local Shading", &localShading); 
  ImGui::Checkbox("Density to Hue", &mColorize);
  if (mColorize && ImGui::DragFloat2("Hue Range", mHueRange.data(), 0, 1)) mBakeDirty = true;
  ImGui::End();

  mShadingMode = localShading ? ShadingMode::eLocal : ShadingMode::eNone;
}

void VolumeRenderer::bake(CommandBuffer& commandBuffer) {
  if (!mRawVolume || (!mBakeDirty && !mGradientDirty)) return;

  uniforms->VolumeRotation = WorldRotation();
  uniforms->InvVolumeRotation = inverse(uniforms->VolumeRotation);
  uniforms->VolumeScale = WorldScale();
  mBakeMaterial->push_constant("gDensity", mDensityScale);
  uniforms->InvVolumeScale = 1.f / uniforms->InvVolumeScale;
  uniforms->MaskValue = (uint32_t)mOrganMask;
  uniforms->VolumePosition = WorldPosition();
  uniforms->FrameIndex = (uint32_t)(commandBuffer.mDevice.FrameCount() % 0x00000000FFFFFFFFull);
  uniforms->RemapRange = mRemapRange;
  uniforms->HueRange = mHueRange;
	uniforms->VolumeResolution = { mRawVolume->extent().width, mRawVolume->extent().height, mRawVolume->extent().depth };

  if (mRawMask) defines.emplace("MASK");
  if (channel_count(mRawVolume->format()) == 1) {
    defines.emplace("SINGLE_CHANNEL");
    if (mColorize) defines.emplace("COLORIZE");
  }

  mBakeMaterial->descriptor("gVolume") = storage_texture_descriptor(mRawVolume);
  mBakeMaterial->descriptor("gVolumeRW") = storage_texture_descriptor(mBakedVolume);

  // Bake the volume if necessary
  if (mBakeDirty && mBakedVolume) {
    if (mRawMask) mBakeMaterial->descriptor("gMask") = storage_texture_descriptor(mRawMask);
    mBakeMaterial->bind(commandBuffer, {});
    mBakeMaterial->bind_descriptor_sets(commandBuffer);
    mBakeMaterial->push_constants(commandBuffer);
    commandBuffer.dispatch_align(mRawVolume->extent().width, mRawVolume->extent().height, mRawVolume->extent().depth);

    commandBuffer.barrier(*mBakedVolume);
    mBakeDirty = false;
  }

  // Bake the gradient if necessary
  if (mGradientDirty && mGradient) {
    ComputePipeline* pipeline = mPrecomputePipeline->GetCompute("BakeGradient", defines);
    commandBuffer.bind_pipeline(pipeline);

    auto ds = commandBuffer.GetDescriptorSet("BakeGradient", pipeline->mDescriptorSetLayouts[0]);
    if (mBakedVolume)
      ds->descriptor("gVolume") =  storage_texture_descriptor(mBakedVolume);
    else {
      ds->descriptor("gVolume") =  storage_texture_descriptor(mRawVolume);
      if (mRawMask) ds->WriteTexture("RawMask", mRawMask);
    }
    ds->descriptor("gVolumeRW") = storage_texture_descriptor(mGradient);
    commandBuffer.bind_descriptor_set(ds, 0);

    commandBuffer.dispatch_align(mRawVolume->extent().width, mRawVolume->extent().height, mRawVolume->extent().depth);

    commandBuffer.barrier(*mBakedVolume);
    mGradientDirty = false;
  }
}

void VolumeRenderer::draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer) {
  if (!mRawVolume && !mBakedVolume) return;

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
	uniforms->VolumeResolution = { mRawVolume->extent().width, mRawVolume->extent().height, mRawVolume->extent().depth };

  if (mRawMask) mRenderMaterial->specialization_constant("gMask", true);
  if (mBakedVolume) mRenderMaterial->specialization_constant("gBakedColor", true);
  else if (channel_count(mRawVolume->format()) == 1) {
    mRenderMaterial->specialization_constant("gSingleChannel", true);
    if (mColorize) mRenderMaterial->specialization_constant("gColorize", true);
  }
  switch (mShadingMode) {
    case ShadingMode::eLocal:
      mRenderMaterial->specialization_constant("gLocalShading", true);
      break;
  }
  if (mGradient) mRenderMaterial->specialization_constant("gBakedGradient", true);

  auto renderTarget = framebuffer->at("stm_main_resolve");

  mRenderMaterial->descriptor("gRenderTarget") = storage_texture_descriptor(renderTarget);
  mRenderMaterial->descriptor("gDepthBuffer") = sampled_texture_descriptor(framebuffer->at("stm_main_depth"));
  mRenderMaterial->descriptor("gVolume") = storage_texture_descriptor(mBakedVolume ? mBakedVolume : mRawVolume);
  if (mRawMask) mRenderMaterial->descriptor("gMask") = storage_texture_descriptor(mRawMask);
  if (mGradient) mRenderMaterial->descriptor("gGradient") = storage_texture_descriptor(mGradient);

  mRenderMaterial->push_constant("gCameraPosition", camPos[0]);
  mRenderMaterial->push_constant("gInvViewProj", camera.InverseViewProjection(StereoEye::eLeft));
  mRenderMaterial->push_constant("gSampleRate", mSampleRate);
  
  mRenderMaterial->bind(commandBuffer, {});

  vk::Extent2D res = renderTarget->extent();
  switch (camera.StereoMode()) {
  case StereoMode::eNone:
    commandBuffer.push_constant("gWriteOffset", Vector2i::Zero());
    commandBuffer.push_constant("gScreenResolution", res);
    commandBuffer.dispatch_align(res);
    break;
  case StereoMode::eHorizontal:
    res.width /= 2;
    // left eye
    commandBuffer.push_constant("gScreenResolution", res);
    commandBuffer.push_constant("gWriteOffset", Vector2i::Zero());
    commandBuffer.dispatch_align(res);
    // right eye
    commandBuffer.push_constant("InvViewProj", camera.InverseViewProjection(StereoEye::eRight));
    commandBuffer.push_constant("gCameraPosition", camPos[1]);
    commandBuffer.push_constant("gWriteOffset", Vector2i(res.width, 0));
    commandBuffer.dispatch_align(res);
    break;
  case StereoMode::eVertical:
    res.height /= 2;
    // left eye
    commandBuffer.push_constant("gScreenResolution", res);
    commandBuffer.push_constant("gWriteOffset", Vector2i::Zero());
    commandBuffer.dispatch_align(res);
    // right eye
    commandBuffer.push_constant("gInvViewProj", camera.InverseViewProjection(StereoEye::eRight));
    commandBuffer.push_constant("gCameraPosition", camPos[1]);
    commandBuffer.push_constant("gWriteOffset", Vector2i(0, res.height));
    commandBuffer.dispatch_align(res);
    break;
  }
}