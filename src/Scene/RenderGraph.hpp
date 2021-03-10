#pragma once

#include "../Core/Framebuffer.hpp"
#include "Camera.hpp"

namespace stm {

class Renderer : virtual public SceneNode::Component {
public:
	inline virtual bool Visible(Camera& camera) = 0;

protected:
	friend class RenderGraph;

	// Called before a Camera begins rendering the scene
	inline virtual void OnPreRenderCamera(CommandBuffer& commandBuffer, Camera& camera) {}
	// Called when a Camera draws this Renderer
	virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) = 0;
	// Called after a Camera renders the scene
	inline virtual void OnRenderCamera(CommandBuffer& commandBuffer, Camera& camera) {}

	// Called after a RenderPass ends
	inline virtual void OnEndRenderPass(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, const vector<Camera*>& cameras) {}
};

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