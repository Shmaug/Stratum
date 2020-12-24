#pragma once

#include "../../Core/Asset/Mesh.hpp"
#include "Renderer.hpp"

namespace stm {

// Renders a mesh with a material
// The scene will attempt to batch MeshRenderers that share the same mesh and material that have an 'Instances' parameter, and use instancing to render them all at once
class MeshRenderer : public Renderer {
public:
	inline virtual void Mesh(shared_ptr<stm::Mesh> m) { mMesh = m; InvalidateTransform(); }
	inline virtual shared_ptr<stm::Mesh> Mesh() const { return mMesh; }

	inline virtual optional<fAABB> Bounds() override { ValidateTransform(); return mAABB; }
	STRATUM_API virtual bool Intersect(const fRay& ray, float* t, bool any) override;

	inline virtual bool Visible(const string& pass) override { return mMesh && mMaterial && Renderer::Visible(pass); }

private:
	shared_ptr<stm::Mesh> mMesh;
	optional<fAABB> mAABB;

protected:
	friend class Scene;
	inline MeshRenderer(const string& name, stm::Scene& scene) : Object(name, scene) {};

	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override;
	STRATUM_API virtual void OnDrawInstanced(CommandBuffer& commandBuffer, Camera& camera, const shared_ptr<Buffer>& instanceBuffer, uint32_t instanceCount) override;
	STRATUM_API virtual bool TryCombineInstances(CommandBuffer& commandBuffer, Renderer* renderer, shared_ptr<Buffer>& instanceBuffer, uint32_t& instanceCount) override;

	STRATUM_API bool ValidateTransform() override;
};

}