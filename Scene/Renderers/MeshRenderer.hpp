#pragma once

#include <Scene/Renderers/Renderer.hpp>
#include <Data/Material.hpp>
#include <Data/Mesh.hpp>

// Renders a mesh with a material
// The scene will attempt to batch MeshRenderers that share the same mesh and material that have an 'Instances' parameter, and use instancing to render them all at once
class MeshRenderer : public Renderer {
public:
	STRATUM_API MeshRenderer(const std::string& name);
	STRATUM_API ~MeshRenderer();

	STRATUM_API virtual void Mesh(variant_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh.get(); }

	inline virtual void Material(variant_ptr<::Material> m) { mMaterial = m; }
	inline virtual ::Material* Material() { return mMaterial.get(); }

	// Renderer functions

	inline virtual bool BypassCulling() override { return mBypassCulling; }
	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }
	STRATUM_API virtual bool Intersect(const Ray& ray, float* t, bool any) override;

	inline virtual bool Visible(const std::string& pass) override { return mMesh.get() && mMaterial.get() && mMaterial->GetPassPipeline(pass) && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial.get() ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }

private:
	variant_ptr<::Mesh> mMesh;
	variant_ptr<::Material> mMaterial;
	bool mBypassCulling;
	AABB mAABB;

protected:
	STRATUM_API virtual void OnLateUpdate(CommandBuffer* commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) override;
	STRATUM_API virtual void OnDrawInstanced(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera, Buffer* instanceBuffer, uint32_t instanceCount) override;
	STRATUM_API virtual bool TryCombineInstances(CommandBuffer* commandBuffer, Renderer* renderer, Buffer*& instanceBuffer, uint32_t& instanceCount) override;

	STRATUM_API bool UpdateTransform() override;
};