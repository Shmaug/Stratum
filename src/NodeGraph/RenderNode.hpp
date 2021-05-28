#pragma once

#include "NodeGraph.hpp"

#include "../Core/Material.hpp"

namespace stm {

using SubpassIdentifier = string;

class RenderNode {
private:
  NodeGraph::Node& mNode;
  vector<pair<SubpassIdentifier, RenderPass::SubpassDescription>> mSubpasses;
  shared_ptr<RenderPass> mRenderPass;

  inline void validate_renderpass(Device& device) {
    if (!mRenderPass) {
      ProfilerRegion ps("RenderNode::validate_renderpass");
      vector<RenderPass::SubpassDescription> subpasses(mSubpasses.size());
      ranges::transform(mSubpasses, subpasses.begin(), &pair<SubpassIdentifier, RenderPass::SubpassDescription>::second);
      mRenderPass = make_shared<RenderPass>(device, mNode.name()+"/RenderPass", subpasses);
    }
  }

public:
  NodeGraph::Event<CommandBuffer&> PreRender;
  NodeGraph::Event<CommandBuffer&> OnRender;

  RenderNode(RenderNode&&) = default;
  inline RenderNode(NodeGraph::Node& node, const unordered_map<SubpassIdentifier, RenderPass::SubpassDescription>& subpasses = {}) : mNode(node) {
    mSubpasses.resize(subpasses.size());
    ranges::copy(subpasses, mSubpasses.begin());
  }

	inline NodeGraph::Node& node() const { return mNode; }
\
  template<ranges::range R>
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const R& renderTargets, const vk::ArrayProxy<const vk::ClearValue>& clearValues = {}) {
    validate_renderpass(commandBuffer.mDevice);
    
    ProfilerRegion ps("RenderNode::render", commandBuffer);
    auto framebuffer = make_shared<Framebuffer>(*mRenderPass, mNode.name()+"/Framebuffer", renderTargets);

    PreRender(mNode.node_graph(), commandBuffer);
    
    commandBuffer.begin_render_pass(mRenderPass, framebuffer, clearValues);
    commandBuffer->setViewport(0, { vk::Viewport(0, (float)framebuffer->extent().height, (float)framebuffer->extent().width, -(float)framebuffer->extent().height, 0, 1) });
    commandBuffer->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->extent()) });

    OnRender(mNode.node_graph(), commandBuffer);
    for (uint32_t i = 1; i < mRenderPass->subpasses().size(); i++) {
      commandBuffer.next_subpass();
      OnRender(mNode.node_graph(), commandBuffer);
    }
    commandBuffer.end_render_pass();

    return framebuffer;
  }
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, Texture::View>& renderTargets) {
    validate_renderpass(commandBuffer.mDevice);
    
    vk::Extent3D extent(0,0,1);
    vector<Texture::View> attachments(mRenderPass->attachments().size());
    for (auto[id,view] : renderTargets) {
      attachments[mRenderPass->attachment_index(id)] = view;
      extent.width = max(extent.width, view.texture().extent().width);
      extent.height = max(extent.height, view.texture().extent().height);
    }
    for (uint32_t i = 0; i < attachments.size(); i++)
      if (!attachments[i]) {
        auto[info, id] = mRenderPass->attachments()[i];
        vk::ImageUsageFlagBits usageFlags;
        if (is_depth_stencil(info.format))
          usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment;
        else
          usageFlags = vk::ImageUsageFlagBits::eColorAttachment;
        attachments[i] = make_shared<Texture>(commandBuffer.mDevice, id, extent, info, usageFlags);
      }
    return render(commandBuffer, attachments);
  }
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, pair<Texture::View, vk::ClearValue>>& renderTargets) {
    validate_renderpass(commandBuffer.mDevice);

    vk::Extent2D extent(0,0);
    vector<Texture::View> attachments(mRenderPass->attachments().size());
    vector<vk::ClearValue> clearValues(mRenderPass->attachments().size());
    for (auto[id,p] : renderTargets) {
      uint32_t index = mRenderPass->attachment_index(id);
      attachments[index] = p.first;
      clearValues[index] = p.second;
      extent = vk::Extent2D(max(extent.width, p.first.texture().extent().width), max(extent.height, p.first.texture().extent().height));
    }
    return render(commandBuffer, attachments, clearValues);
  }
  
  inline uint32_t subpass_index(const SubpassIdentifier& id) const {
    for (uint32_t i = 0; i < mSubpasses.size(); i++)
      if (mSubpasses[i].first == id)
        return i;
    return -1;
  }
  
  // TODO: caller can store the reference and modify it later, which won't invalidate the renderpass...
  inline auto& operator[](uint32_t index) {
    mRenderPass = nullptr;
    return mSubpasses[index];
  }
  inline auto& operator[](const SubpassIdentifier& id) {
    uint32_t index = subpass_index(id);
    if (index > mSubpasses.size()) {
      index = (uint32_t)mSubpasses.size();
      mSubpasses.emplace_back(make_pair(id, RenderPass::SubpassDescription()));
    }
    return operator[](index).second;
  }
  inline const auto& at(uint32_t index) const { return mSubpasses[index]; }
  inline const auto& at(const SubpassIdentifier& id) const { return at(subpass_index(id)).second; }
  
  inline void erase(uint32_t index) {
    mSubpasses.erase(mSubpasses.begin() + index);
    mRenderPass = nullptr;
  }
  inline void erase(const SubpassIdentifier& id) {
    uint32_t index = subpass_index(id);
    if (index < mSubpasses.size())
      erase(index);
  }
};

}