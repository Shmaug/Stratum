#pragma once

#include <Scene/Renderers/Renderer.hpp>
#include <Scene/Bone.hpp>

typedef std::vector<Bone*> AnimationRig;

class SkinnedMeshRenderer : public Renderer {
public:
	inline SkinnedMeshRenderer(const std::string& name) : Object(name) {}
	STRATUM_API ~SkinnedMeshRenderer();

	STRATUM_API virtual AnimationRig& Rig() { return mRig; };
	STRATUM_API virtual void Rig(const AnimationRig& rig);
	STRATUM_API virtual Bone* GetBone(const std::string& name) const;

	inline virtual void Mesh(variant_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh.get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	STRATUM_API virtual void Material(variant_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions
protected:
	inline virtual bool Visible(const std::string& pass) override { return mMesh.get() && mMaterial.get() && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial.get() ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }
	
	STRATUM_API virtual void OnLateUpdate(CommandBuffer* commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer* commandBuffer, Camera* camera, DescriptorSet* perCamera) override;
	
	STRATUM_API void OnGui(CommandBuffer* commandBuffer, Camera* camera, GuiContext* gui);

private:
	variant_ptr<::Material> mMaterial;
	variant_ptr<::Mesh> mMesh;
	AABB mAABB;
	::Mesh* mSkinnedMesh;

	std::unordered_map<std::string, Bone*> mBoneMap;
	AnimationRig mRig;
};