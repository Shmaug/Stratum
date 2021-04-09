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
  Delegate<RenderNode*, const RenderPass::SubpassDescription&> OnRenderSubpass;

  inline RenderNode(Scene& scene, const string& name) : Node(scene, name) {}

  inline shared_ptr<Framebuffer> Render(CommandBuffer& commandBuffer) {  
    ProfilerRegion ps("RenderGraph::Render", commandBuffer);
    
    if (!mRenderPass) {
      // Create new renderpasses/framebuffers
      vector<RenderPass::SubpassDescription> subpasses(mSubpasses.size());
      ranges::copy(ranges::values(mSubpasses), subpasses.begin());
      vector<shared_ptr<Texture>> attachments;
      // TODO: this
      mRenderPass = make_shared<RenderPass>(mName+"/RenderPass", commandBuffer.mDevice, subpasses);
      mFramebuffer = make_shared<Framebuffer>(mName+"/Framebuffer", commandBuffer.mDevice, attachments);
    }

    commandBuffer.BeginRenderPass(mRenderPass, mFramebuffer);
    
    for (const auto& subpass : mRenderPass->SubpassDescriptions()) {
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
    if (mSubpasses.erase(ranges::find(mSubpasses, name, RenderPass::SubpassDescription::mName))
      mRenderPass = nullptr;
  }
};

}