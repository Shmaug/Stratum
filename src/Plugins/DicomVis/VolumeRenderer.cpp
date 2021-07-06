#include "VolumeRenderer.hpp"
#include "ImageLoader.hpp"
#include "../../Core/Window.hpp"

using namespace dcmvs;

VolumeRenderer::VolumeRenderer(NodeGraph::Node& node, const Texture::View& volume, const Texture::View& mask, const Vector3f& voxelSize)
  : mNode(node), mRawVolume(volume), mRawMask(mask), mVoxelSize(voxelSize) {
  
  mBakeColorMaterial = make_shared<Material>("bake_color");
  mBakeGradientMaterial = make_shared<Material>("bake_gradient");
  mRenderMaterial = make_shared<Material>("render");

  mBakeColorMaterial->descriptor("gVolume") = storage_texture_descriptor(mRawVolume);
  mBakeColorMaterial->descriptor("gVolumeRW") = storage_texture_descriptor(mBakedVolume);
  mBakeColorMaterial->descriptor("gMask") = storage_texture_descriptor(mRawMask);

  mBakeGradientMaterial->descriptor("gVolume") =  storage_texture_descriptor(mBakedVolume ? mBakedVolume : mRawVolume);
  mBakeGradientMaterial->descriptor("gMask") = storage_texture_descriptor(mRawMask);
  mBakeGradientMaterial->descriptor("gVolumeRW") = storage_texture_descriptor(mGradient);

  mRenderMaterial->descriptor("gMask") = storage_texture_descriptor(mRawMask);
  mRenderMaterial->descriptor("gGradient") = storage_texture_descriptor(mGradient);
  mRenderMaterial->descriptor("gVolume") = storage_texture_descriptor(mBakedVolume ? mBakedVolume : mRawVolume);

  mBakeColorMaterial->push_constant("gVolumeResolution", volume.texture()->extent());
  mBakeGradientMaterial->push_constant("gVolumeResolution", volume.texture()->extent());
  mRenderMaterial->push_constant("gVolumeResolution", volume.texture()->extent());
}

void VolumeRenderer::imgui() {
  bool localShading = mShadingMode == ShadingMode::eLocal;
  if (ImGui::Begin("Render Settings")) {

    if (ImGui::SliderFloat("Density Scale", &mDensityScale, 0, 100))
      mRenderMaterial->push_constant("gDensity", mDensityScale);

    if (ImGui::DragFloat2("Remap Density", mRemapRange.data(), 1, 0, 1)) {
      mBakeDirty = true;
      mGradientDirty = true;
      mRenderMaterial->push_constant("gRemapRange", mRemapRange);
      mBakeColorMaterial->push_constant("gRemapRange", mRemapRange);
      mBakeGradientMaterial->push_constant("gRemapRange", mRemapRange);
    }
    
    if (ImGui::Checkbox("Density to Hue", &mColorize)) {
      mBakeColorMaterial->specialization_constant("gColorize", mColorize);
      mBakeDirty = true;
    }
    if (mColorize && ImGui::DragFloat2("Hue Range", mHueRange.data(), 0, 1)) {
      mBakeColorMaterial->push_constant("gHueRange", mHueRange);
      mBakeDirty = true;
    }

    if (ImGui::Checkbox("Local Shading", &localShading))
      mRenderMaterial->push_constant("gLocalShading", localShading); 
  }
  ImGui::End();
  mShadingMode = localShading ? ShadingMode::eLocal : ShadingMode::eNone;
}

void VolumeRenderer::bake(CommandBuffer& commandBuffer) {
  if (!mRawVolume || (!mBakeDirty && !mGradientDirty)) return;

  // Bake the volume if necessary
  if (mBakeDirty && mBakedVolume) {
    mBakeColorMaterial->push_constant("gMasked", (bool)mRawMask);
    mBakeColorMaterial->bind(commandBuffer, {});
    mBakeColorMaterial->bind_descriptor_sets(commandBuffer);
    mBakeColorMaterial->push_constants(commandBuffer);
    commandBuffer.dispatch_align(mRawVolume.texture()->extent());
    mBakedVolume.transition_barrier(commandBuffer, vk::ImageLayout::eGeneral, true);
    mBakeDirty = false;
  }

  // Bake the gradient if necessary
  if (mGradientDirty && mGradient) {
    mBakeGradientMaterial->specialization_constant("gMasked", (bool)mRawMask);
    mBakeGradientMaterial->specialization_constant("gColorBaked", (bool)mBakedVolume);
    mBakeGradientMaterial->bind(commandBuffer, {});
    mBakeGradientMaterial->bind_descriptor_sets(commandBuffer);
    mBakeGradientMaterial->push_constants(commandBuffer);
    commandBuffer.dispatch_align(mRawVolume.texture()->extent());
    mGradient.transition_barrier(commandBuffer, vk::ImageLayout::eGeneral, true);
    mGradientDirty = false;
  }

  switch (mShadingMode) {
    case ShadingMode::eLocal:
      mRenderMaterial->specialization_constant("gLocalShading", true);
      break;
  }
  mRenderMaterial->specialization_constant("gMasked", (bool)mRawMask);
  mRenderMaterial->specialization_constant("gColorBaked", (bool)mBakedVolume);
  mRenderMaterial->specialization_constant("gGradientBaked", (bool)mGradient);
  mRenderMaterial->push_constant("gSampleRate", mSampleRate);
}

void VolumeRenderer::draw(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer) {
  if (!mRawVolume && !mBakedVolume) return;

  auto renderTarget = framebuffer->at("primaryResolve");

  vk::Extent2D res(renderTarget.texture()->extent().width, renderTarget.texture()->extent().height);
  mRenderMaterial->descriptor("gRenderTarget") = storage_texture_descriptor(renderTarget);
  mRenderMaterial->descriptor("gDepthBuffer") = sampled_texture_descriptor(framebuffer->at("primaryDepth"));
  mRenderMaterial->push_constant("gCameraToWorld", cameraTransform);
  mRenderMaterial->push_constant("gProjection", cameraProjection);
  mRenderMaterial->push_constant("gVolumeTransform", mNode.get<hlsl::TransformData>());
  mRenderMaterial->push_constant("gScreenResolution", res);
  mRenderMaterial->push_constant("gWriteOffset", Vector2i::Zero());
  mRenderMaterial->push_constant("gFrameIndex", commandBuffer.mDevice.mInstance.window().present_count());

  mRenderMaterial->bind(commandBuffer, {});
  mRenderMaterial->bind_descriptor_sets(commandBuffer);
  mRenderMaterial->push_constants(commandBuffer);
  commandBuffer.dispatch_align(res);
}