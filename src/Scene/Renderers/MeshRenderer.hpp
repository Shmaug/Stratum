#pragma once

#include "../Data/Material.hpp"
#include "../Data/Mesh.hpp"

#include "Renderer.hpp"

namespace stm {

// Renders a mesh with a material
// The scene will attempt to batch MeshRenderers that share the same mesh and material that have an 'Instances' parameter, and use instancing to render them all at once
class MeshRenderer : public Renderer {
public:
	inline MeshRenderer(const std::string& name, Scene* scene) : Object(name, scene) {};

	inline virtual void Mesh(std::shared_ptr<stm::Mesh> m) { mMesh = m; DirtyTransform(); }
	inline virtual std::shared_ptr<stm::Mesh> Mesh() const { return mMesh; }

	inline virtual void Material(std::shared_ptr<stm::Material> m) { mMaterial = m; }
	inline virtual std::shared_ptr<stm::Material> Material() { return mMaterial; }


	inline virtual std::optional<AABB> Bounds() override { UpdateTransform(); return mAABB; }
	STRATUM_API virtual bool Intersect(const Ray& ray, float* t, bool any) override;

	inline virtual bool Visible(const std::string& pass) override { return mMesh && mMaterial && mMaterial->GetPassPipeline(pass) && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override {
		if (mMaterial)
			if (GraphicsPipeline* p = mMaterial->GetPassPipeline(pass))
				return p->mShaderVariant->mRenderQueue;
		return Renderer::RenderQueue(pass);
	}

private:
	std::shared_ptr<stm::Mesh> mMesh;
	std::shared_ptr<stm::Material> mMaterial;
	std::optional<AABB> mAABB;

protected:
	STRATUM_API virtual void OnLateUpdate(CommandBuffer& commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera, const std::shared_ptr<DescriptorSet>& perCamera) override;
	STRATUM_API virtual void OnDrawInstanced(CommandBuffer& commandBuffer, Camera& camera, const std::shared_ptr<DescriptorSet>& perCamera, const std::shared_ptr<Buffer>& instanceBuffer, uint32_t instanceCount) override;
	STRATUM_API virtual bool TryCombineInstances(CommandBuffer& commandBuffer, Renderer* renderer, std::shared_ptr<Buffer>& instanceBuffer, uint32_t& instanceCount) override;

	STRATUM_API bool UpdateTransform() override;
};

}