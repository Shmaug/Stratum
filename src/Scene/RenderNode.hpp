#pragma once

#include "Scene.hpp"

#include "../Core/Material.hpp"
#include "../Core/Mesh.hpp"

namespace stm {

using SubpassIdentifier = string;

class RenderNode : public Scene::Node {
private:
  vector<pair<SubpassIdentifier, RenderPass::SubpassDescription>> mSubpasses;
  shared_ptr<RenderPass> mRenderPass;

public:
  Event<CommandBuffer&, const RenderNode&> OnRender;

  inline RenderNode(Scene& scene, const string& name, const unordered_map<SubpassIdentifier, RenderPass::SubpassDescription>& subpasses = {})
    : Node(scene, name), OnRender(this) {
    mSubpasses.resize(subpasses.size());
    ranges::copy(subpasses, mSubpasses.begin());
  }

  template<ranges::range R>
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const R& attachments, const vk::ArrayProxy<const vk::ClearValue>& clearValues = {}) {
    ProfilerRegion ps("RenderNode::render", commandBuffer);
    
    if (!mRenderPass) {
      vector<RenderPass::SubpassDescription> subpasses(mSubpasses.size());
      ranges::copy(mSubpasses | views::values, subpasses.begin()); // TODO: sort subpasses
      mRenderPass = make_shared<RenderPass>(commandBuffer.mDevice, name()+"/RenderPass", subpasses);
    }
    auto framebuffer = make_shared<Framebuffer>(*mRenderPass, name()+"/Framebuffer", attachments);

    commandBuffer.begin_render_pass(mRenderPass, framebuffer, clearValues);
    commandBuffer->setViewport(0, { vk::Viewport(0, (float)framebuffer->extent().height, (float)framebuffer->extent().width, -(float)framebuffer->extent().height, 0, 1) });
    commandBuffer->setScissor(0, { vk::Rect2D(vk::Offset2D(0,0), framebuffer->extent()) });

    OnRender(commandBuffer, *this);
    for (uint32_t i = 1; i < mRenderPass->subpasses().size(); i++) {
      commandBuffer.next_subpass();
      OnRender(commandBuffer, *this);
    }
    commandBuffer.end_render_pass();

    return framebuffer;
  }
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, Texture::View>& attachments) {
    return render(commandBuffer, attachments|views::values);
  }
  inline shared_ptr<Framebuffer> render(CommandBuffer& commandBuffer, const unordered_map<RenderAttachmentId, pair<Texture::View, vk::ClearValue>>& attachments) {
    vector<vk::ClearValue> clearValues(attachments.size());
    ranges::copy(attachments|views::values|views::elements<1>, clearValues.begin());
    return render(commandBuffer, attachments|views::values|views::elements<0>, clearValues);
  }
  
  inline uint32_t find(const SubpassIdentifier& id) const {
    for (uint32_t i = 0; i < mSubpasses.size(); i++)
      if (mSubpasses[i].first == id)
        return i;
    return -1;
  }
  
  // TODO: caller can store the reference and modify it later, which will not invalidate the renderpass
  inline auto& operator[](uint32_t index) {
    mRenderPass = nullptr;
    return mSubpasses[index];
  }
  inline auto& operator[](const SubpassIdentifier& id) {
    uint32_t index = find(id);
    if (index > mSubpasses.size()) {
      index = (uint32_t)mSubpasses.size();
      mSubpasses.emplace_back(make_pair(id, RenderPass::SubpassDescription()));
    }
    return operator[](index).second;
  }
  inline const auto& at(uint32_t index) const { return mSubpasses[index]; }
  inline const auto& at(const SubpassIdentifier& id) const { return at(find(id)).second; }
  
  inline void erase(uint32_t index) {
    mSubpasses.erase(mSubpasses.begin() + index);
    mRenderPass = nullptr;
  }
  inline void erase(const SubpassIdentifier& id) {
    uint32_t index = find(id);
    if (index < mSubpasses.size())
      erase(index);
  }
};

}