#pragma once

#include "Scene.hpp"

#include "../Core/CommandBuffer.hpp"
#include "../Core/Mesh.hpp"

namespace stm {

class RenderNode : public Scene::Node {
private:
  vector<RenderPass::SubpassDescription> mSubpasses;
  shared_ptr<RenderPass> mRenderPass;
  shared_ptr<Framebuffer> mFramebuffer;

public:
  Scene::NodeDelegate<RenderNode*, const RenderPass::SubpassDescription&> OnRenderSubpass;

  inline RenderNode(Scene& scene, const string& name) : Node(scene, name) {}

  inline shared_ptr<Framebuffer> Render(CommandBuffer& commandBuffer) {  
    ProfilerRegion ps("RenderGraph::Render", commandBuffer);
    
    if (!mRenderPass) {
      // Create new renderpasses/framebuffers
      vector<RenderPass::SubpassDescription> subpasses(mSubpasses.size());
      vector<TextureView> attachments;
      
      // TODO: this

      mRenderPass = make_shared<RenderPass>(mName+"/RenderPass", commandBuffer.mDevice, subpasses);
      mFramebuffer = make_shared<Framebuffer>(mName+"/Framebuffer", *mRenderPass, attachments);
    }

    vector<vk::ClearValue> clearValues(mFramebuffer->size());
    commandBuffer.BeginRenderPass(mRenderPass, mFramebuffer, clearValues);
    for (const RenderPass::SubpassDescription& subpass : mRenderPass->SubpassDescriptions()) {
      ProfilerRegion ps(subpass.mName, commandBuffer);
      OnRenderSubpass(this, subpass);
      if (mRenderPass->SubpassDescriptions().size() > 1)
        commandBuffer.NextSubpass();
    }
    commandBuffer.EndRenderPass();
    return mFramebuffer;
  }

  inline void push_back(const RenderPass::SubpassDescription& subpass) {
    mSubpasses.push_back(subpass);
    mRenderPass = nullptr;
  }
  inline void erase(const string& name) {
    auto it = ranges::find(mSubpasses, name, &RenderPass::SubpassDescription::mName);
    if (it != mSubpasses.end())
      mSubpasses.erase(it);
      mRenderPass = nullptr;
  }
};

}