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

	STRATUM_API virtual void Mesh(stm_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh; }

	inline virtual void Material(stm_ptr<::Material> m) { mMaterial = m; }
	inline virtual ::Material* Material() { return mMaterial; }

	// Renderer functions

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
	stm_ptr<::Mesh> mMesh;
	stm_ptr<::Material> mMaterial;
	std::optional<AABB> mAABB;

protected:
	STRATUM_API virtual void OnLateUpdate(stm_ptr<CommandBuffer> commandBuffer) override;
	STRATUM_API virtual void OnDraw(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera) override;
	STRATUM_API virtual void OnDrawInstanced(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera, stm_ptr<Buffer> instanceBuffer, uint32_t instanceCount) override;
	STRATUM_API virtual bool TryCombineInstances(stm_ptr<CommandBuffer> commandBuffer, Renderer* renderer, stm_ptr<Buffer>& instanceBuffer, uint32_t& instanceCount) override;

	STRATUM_API bool UpdateTransform() override;
};