#pragma once

#include <Content/Material.hpp>
#include <Content/Mesh.hpp>
#include <Core/DescriptorSet.hpp>
#include <Scene/Renderer.hpp>
#include <Util/Util.hpp>

// Renders a mesh with a material
// The scene will attempt to batch MeshRenderers that share the same mesh and material that have an 'Instances' parameter, and use instancing to render them all at once
class MeshRenderer : public Renderer {
public:
	bool mVisible;

	ENGINE_EXPORT MeshRenderer(const std::string& name);
	ENGINE_EXPORT ~MeshRenderer();

	inline virtual void Mesh(::Mesh* m) { mMesh = m; Dirty(); }
	inline virtual void Mesh(std::shared_ptr<::Mesh> m) { mMesh = m; Dirty(); }
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	ENGINE_EXPORT virtual void Material(std::shared_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions

	ENGINE_EXPORT virtual bool Intersect(const Ray& ray, float* t, bool any) override;
	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }

	inline virtual PassType PassMask() override { return mMaterial ? mMaterial->PassMask() : Renderer::PassMask(); }
	inline virtual bool Visible() override { return mVisible && Mesh() && mMaterial && EnabledHierarchy(); }
	inline virtual uint32_t RenderQueue() override { return mMaterial ? mMaterial->RenderQueue() : Renderer::RenderQueue(); }
	
	ENGINE_EXPORT virtual bool TryCombineInstances(CommandBuffer* commandBuffer, Renderer* renderer, Buffer*& instanceBuffer, uint32_t& instanceCount) override;

	ENGINE_EXPORT virtual void PreBeginRenderPass(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;
	ENGINE_EXPORT virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;
	ENGINE_EXPORT virtual void DrawInstanced(CommandBuffer* commandBuffer, Camera* camera, PassType pass, Buffer* instanceBuffer, uint32_t instanceCount) override;

private:
	std::variant<::Mesh*, std::shared_ptr<::Mesh>> mMesh;
	std::shared_ptr<::Material> mMaterial;
	uint32_t mRayMask;
	AABB mAABB;

	ENGINE_EXPORT bool UpdateTransform() override;
};