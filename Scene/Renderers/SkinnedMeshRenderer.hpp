#pragma once

#include <Scene/Renderers/Renderer.hpp>
#include <Scene/Bone.hpp>

typedef std::vector<Bone*> AnimationRig;

class SkinnedMeshRenderer : public Renderer {
public:
	inline SkinnedMeshRenderer(const std::string& name) : Object(name) {}

	STRATUM_API virtual AnimationRig& Rig() { return mRig; };
	STRATUM_API virtual void Rig(const AnimationRig& rig);
	STRATUM_API virtual Bone* GetBone(const std::string& name) const;

	inline virtual void Mesh(stm_ptr<::Mesh> m);
	inline virtual ::Mesh* Mesh() const { return mMesh; }

	inline virtual ::Material* Material() { return mMaterial; }
	STRATUM_API virtual void Material(stm_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions
protected:
	inline virtual bool Visible(const std::string& pass) override { return mMesh && mMaterial && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }
	
	STRATUM_API virtual void OnLateUpdate(stm_ptr<CommandBuffer> commandBuffer) override;
	STRATUM_API virtual void OnDraw(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, stm_ptr<DescriptorSet> perCamera) override;
	
	STRATUM_API void OnGui(stm_ptr<CommandBuffer> commandBuffer, Camera* camera, GuiContext* gui);

private:
	stm_ptr<::Material> mMaterial;
	stm_ptr<::Mesh> mMesh;
	AABB mAABB;
	stm_ptr<::Mesh> mSkinnedMesh;

	std::unordered_map<std::string, Bone*> mBoneMap;
	AnimationRig mRig;
};