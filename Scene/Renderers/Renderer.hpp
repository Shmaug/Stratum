#pragma once 

#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>

class Renderer : public virtual Object {
public:
	// Renderers are drawn by the scene in order of increasing RenderQueue
	inline virtual uint32_t RenderQueue(const std::string& pass) { return 1000; }
	inline virtual bool Visible(const std::string& pass) { return EnabledHierarchy(); };
	
	inline virtual bool BypassCulling() { return false; };

protected:
	friend class Scene;
	virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) = 0;
	inline virtual void OnDrawInstanced(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera, Buffer* instanceBuffer, uint32_t instanceCount) {};
	inline virtual bool TryCombineInstances(CommandBuffer* commandBuffer, Renderer* renderer, Buffer*& instanceBuffer, uint32_t& totalInstanceCount) { return false; }
};