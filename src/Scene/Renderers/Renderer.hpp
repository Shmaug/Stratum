#pragma once

#include "../Camera.hpp"

namespace stm {

class Renderer : public virtual Object {
public:
	inline virtual void Material(shared_ptr<stm::Material> m) { mMaterial = m; }
	inline virtual shared_ptr<stm::Material> Material() { return mMaterial; }

	// Renderers are drawn by the scene in order of increasing RenderQueue
	inline virtual uint32_t RenderQueue(const string& pass) { return 1000; }
	inline virtual bool Visible(const string& pass) { return Enabled(); };

protected:
	friend class Scene;
	shared_ptr<stm::Material> mMaterial;
	
	virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) = 0;
	inline virtual void OnDrawInstanced(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<Buffer>& instanceBuffer, uint32_t instanceCount) {};
	inline virtual bool TryCombineInstances(CommandBuffer& commandBuffer, Renderer* renderer, shared_ptr<Buffer>& instanceBuffer, uint32_t& totalInstanceCount) { return false; }
};

};