#pragma once

#include "NodeGraph.hpp"
#include <Core/PipelineState.hpp>

namespace stm {

class DynamicRenderPass {
public:
  class Subpass {
  private:
    string mName;
    SubpassDescription mDescription;
  public:
    DynamicRenderPass& mPass;
    NodeEvent<CommandBuffer&> OnDraw;

    Subpass(Subpass&&) = default;
    inline Subpass(DynamicRenderPass& pass, const string& name, const SubpassDescription& desc = {}) : mPass(pass), mName(name), mDescription(desc) {}

    inline const string& name() const { return mName; }
    inline SubpassDescription& description() { return mDescription; }
    inline const SubpassDescription& description() const { return mDescription; }
    inline optional<SubpassDescription::AttachmentInfo> find_attachment(const RenderAttachmentId& id) {
      auto it = mDescription.attachments().find(id);
      if (it == mDescription.attachments().end()) return {};
      return it->second;
    }
    template<typename...Args>  requires(constructible_from<SubpassDescription::AttachmentInfo, Args...>)
    inline auto& emplace_attachment(const RenderAttachmentId& id, Args&&... args) { return mDescription.emplace(id, forward<Args>(args)...); }
    inline auto& attachment(const RenderAttachmentId& id) { return mDescription.at(id); }
  };

  NodeEvent<CommandBuffer&, const shared_ptr<Framebuffer>&> PreProcess;
  NodeEvent<CommandBuffer&, const shared_ptr<Framebuffer>&> PostProcess;

  DynamicRenderPass(DynamicRenderPass&&) = default;
  inline DynamicRenderPass(Node& node) : mNode(node) {}

	inline Node& node() const { return mNode; }
  inline auto& subpasses() { return mSubpasses; }
  inline const auto& subpasses() const { return mSubpasses; }

  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, pair<Texture::View, vk::ClearValue>>& attachments, vk::Rect2D renderArea = {}) {
    ProfilerRegion ps("DynamicRenderPass::render", commandBuffer);

    auto renderPass = make_shared<RenderPass>(commandBuffer.mDevice, mNode.name()+"/RenderPass", mSubpasses | views::transform([](const auto& s) { return s->description(); }));
    
    vk::Extent2D max_extent;
    for (const auto&[id, p] : attachments) {
      max_extent.width  = max(max_extent.width , p.first.texture()->extent().width);
      max_extent.height = max(max_extent.height, p.first.texture()->extent().height);
    }
    if (renderArea.extent.width == 0) renderArea.extent.width = max_extent.width;
    if (renderArea.extent.height == 0) renderArea.extent.height = max_extent.height;

    vector<vk::ClearValue> clearValues(attachments.size());
    vector<Texture::View> attachmentVec(attachments.size());
    for (const auto&[id, p] : attachments) {
      const auto&[tex, clear] = p;
      uint32_t i = renderPass->attachment_index(id);
      attachmentVec[i] = p.first;
      clearValues[i] = p.second;
    }
    
    auto framebuffer = make_shared<Framebuffer>(*renderPass, mNode.name()+"/Framebuffer", attachmentVec);
    
    PreProcess(commandBuffer, framebuffer);

    commandBuffer.begin_render_pass(renderPass, framebuffer, renderArea, clearValues);

    for (const auto& s : mSubpasses) {
      s->OnDraw(commandBuffer);
      if (mSubpasses.size() > 1) commandBuffer.next_subpass();
    }
    commandBuffer.end_render_pass();

    PostProcess(commandBuffer, framebuffer);

    return framebuffer;
  }

private:
  Node& mNode;
  vector<shared_ptr<Subpass>> mSubpasses;
};

}