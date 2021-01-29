#pragma once

#include "../../Core/Framebuffer.hpp"
#include "../Camera.hpp"

namespace stm {

class Renderer : virtual public SceneNode::Component {
public:
	inline virtual bool Visible(Camera& camera) = 0;

protected:
	friend class Scene;

	// Called before a Camera begins rendering the scene
	inline virtual void OnPreRenderCamera(CommandBuffer& commandBuffer, Camera& camera) {}
	// Called when a Camera draws this Renderer
	virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) = 0;
	// Called after a Camera renders the scene
	inline virtual void OnRenderCamera(CommandBuffer& commandBuffer, Camera& camera) {}

	// Called after a RenderPass ends
	inline virtual void OnEndRenderPass(CommandBuffer& commandBuffer, shared_ptr<Framebuffer> framebuffer, const vector<Camera*>& cameras) {}
};

}