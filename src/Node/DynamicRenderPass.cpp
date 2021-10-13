#include "DynamicRenderPass.hpp"

using namespace stm;
using namespace stm::hlsl;

shared_ptr<Framebuffer> DynamicRenderPass::render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, pair<Image::View, vk::ClearValue>>& attachments, vk::Rect2D renderArea) {
  ProfilerRegion ps(mNode.name() + "::DynamicRenderPass::render", commandBuffer);

  auto renderPass = make_shared<RenderPass>(commandBuffer.mDevice, mNode.name()+"/RenderPass", mSubpasses | views::transform([](const auto& s) { return s->description(); }));
  
  vk::Extent2D max_extent;
  for (const auto&[id, p] : attachments) {
    max_extent.width  = max(max_extent.width , p.first.extent().width);
    max_extent.height = max(max_extent.height, p.first.extent().height);
  }
  if (renderArea.extent.width == 0) renderArea.extent.width = max_extent.width;
  if (renderArea.extent.height == 0) renderArea.extent.height = max_extent.height;

  vector<vk::ClearValue> clearValues(attachments.size());
  vector<Image::View> attachmentVec(attachments.size());
  for (const auto&[id, p] : attachments) {
    const auto&[tex, clear] = p;
    uint32_t i = renderPass->attachment_index(id);
    attachmentVec[i] = p.first;
    clearValues[i] = p.second;
  }
  auto framebuffer = make_shared<Framebuffer>(*renderPass, mNode.name()+"/Framebuffer", attachmentVec);
  
  PreProcess(commandBuffer, framebuffer);

  if (ranges::any_of(mSubpasses, [](const shared_ptr<Subpass>& s) { return !s->OnDraw.empty(); })) {
    commandBuffer.begin_render_pass(renderPass, framebuffer, renderArea, clearValues);
    for (const auto& s : mSubpasses) {
      s->OnDraw(commandBuffer);
      if (mSubpasses.size() > 1) commandBuffer.next_subpass();
    }
    commandBuffer.end_render_pass();
  } else {
    for (const auto&[id, p] : attachments) {
      const auto&[image,clearValue] = p;
      const auto& attachment = mSubpasses[0]->attachment(id);
      if (attachment.mType == AttachmentType::eColor && attachment.mDescription.loadOp == vk::AttachmentLoadOp::eClear) {
        image.transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
        commandBuffer->clearColorImage(*commandBuffer.hold_resource(image.image()), vk::ImageLayout::eTransferDstOptimal, clearValue.color, image.subresource_range());
      } else if (attachment.mType == AttachmentType::eDepthStencil && (attachment.mDescription.loadOp == vk::AttachmentLoadOp::eClear || attachment.mDescription.stencilLoadOp == vk::AttachmentLoadOp::eClear)) {
        image.transition_barrier(commandBuffer, vk::ImageLayout::eTransferDstOptimal);
        commandBuffer->clearDepthStencilImage(*commandBuffer.hold_resource(image.image()), vk::ImageLayout::eTransferDstOptimal, clearValue.depthStencil, image.subresource_range());
      }
    }
  }

  PostProcess(commandBuffer, framebuffer);

  return framebuffer;
}