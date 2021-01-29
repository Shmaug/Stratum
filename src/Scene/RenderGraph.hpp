#pragma once

#include "../Core/Framebuffer.hpp"
#include "Camera.hpp"

namespace stm {

class RenderGraph : public SceneNode::Component {
public:
  inline RenderGraph(SceneNode& node, const string& name) : SceneNode::Component(node, name) {}

	STRATUM_API void Validate(CommandBuffer& commandBuffer);

	STRATUM_API shared_ptr<Framebuffer> Render(CommandBuffer& commandBuffer);
	STRATUM_API void RenderCamera(CommandBuffer& commandBuffer, Camera& camera);

  inline void insert(const string& name, const RenderPass::SubpassDescription& subpass) {
    mSubpasses[name] = subpass;
    mValid = false;
  }
  inline void erase(const string& name) {
    if (mSubpasses.erase(name))
      mValid = false;
  }

private:
  unordered_map<string, RenderPass::SubpassDescription> mSubpasses;

  shared_ptr<RenderPass> mRenderPass;
  shared_ptr<Framebuffer> mFramebuffer;
  bool mValid = false;
};

}