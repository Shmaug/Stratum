#pragma once 

#include <Scene/Object.hpp>
#include <Scene/Scene.hpp>
#include <Util/Util.hpp>

class Renderer : public virtual Object {
public:
	// Renderers are drawn by the scene in order of increasing RenderQueue
	inline virtual uint32_t RenderQueue() { return 1000; }
	// Since passes correspond to LayerMasks as well, renderers are only drawn for passes that match PassMask()
	inline virtual PassType PassMask() { return PASS_MAIN; };
	inline virtual bool Visible() { return EnabledHierarchy(); };
	
	ENGINE_EXPORT virtual bool TryCombineInstances(CommandBuffer* commandBuffer, Renderer* renderer, Buffer*& instanceBuffer, uint32_t& instanceCount) { return false; }

	inline virtual void PreBeginRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) {};
	virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) = 0;
	inline virtual void DrawInstanced(CommandBuffer* commandBuffer, Camera* camera, PassType pass, Buffer* instances, uint32_t instanceCount) {};

	// Since passes correspond to LayerMasks as well, renderers are only drawn for passes that match PassMask()
	inline virtual uint32_t LayerMask() override { return Visible() ? Object::LayerMask() | PassMask() : 0; };
};