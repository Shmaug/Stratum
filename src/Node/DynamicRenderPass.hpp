#pragma once

#include <Core/CommandBuffer.hpp>

#include "NodeGraph.hpp"

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

  STRATUM_API shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, pair<Image::View, vk::ClearValue>>& attachments, vk::Rect2D renderArea = {});

private:
  Node& mNode;
  vector<shared_ptr<Subpass>> mSubpasses;
};

}