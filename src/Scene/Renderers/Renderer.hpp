#pragma once 

#include <Scene/Scene.hpp>
#include <Scene/Camera.hpp>

class Renderer : public virtual Object {
public:
	// Renderers are drawn by the scene in order of increasing RenderQueue
	inline virtual uint32_t RenderQueue(const std::string& pass) { return 1000; }
	inline virtual bool Visible(const std::string& pass) { return EnabledHierarchy(); };

protected:
	friend class Scene;
	virtual void OnDraw(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera) = 0;
	inline virtual void OnDrawInstanced(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera, stm_ptr<Buffer> instanceBuffer, uint32_t instanceCount) {};
	inline virtual bool TryCombineInstances(stm_ptr<CommandBuffer> commandBuffer, Renderer* renderer, stm_ptr<Buffer>& instanceBuffer, uint32_t& totalInstanceCount) { return false; }
};