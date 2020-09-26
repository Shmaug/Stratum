#pragma once

#include <Scene/Renderers/Renderer.hpp>
#include <Scene/Bone.hpp>

namespace stm {

typedef std::vector<Bone*> AnimationRig;

class SkinnedMeshRenderer : public Renderer {
public:
	inline SkinnedMeshRenderer(const std::string& name, Scene* scene) : Object(name, scene) {};

	STRATUM_API virtual AnimationRig& Rig() { return mRig; };
	STRATUM_API virtual void Rig(const AnimationRig& rig);
	STRATUM_API virtual Bone* GetBone(const std::string& name) const;

	inline virtual void Mesh(std::shared_ptr<stm::Mesh> m);
	inline virtual std::shared_ptr<stm::Mesh> Mesh() const { return mMesh; }

	inline virtual std::shared_ptr<stm::Material> Material() { return mMaterial; }
	STRATUM_API virtual void Material(std::shared_ptr<stm::Material> m) { mMaterial = m; }


	inline virtual bool Visible(const std::string& pass) override { return mMesh && mMaterial && Renderer::Visible(pass); }
	inline virtual uint32_t RenderQueue(const std::string& pass) override { return mMaterial ? mMaterial->GetPassPipeline(pass)->mShaderVariant->mRenderQueue : Renderer::RenderQueue(pass); }
	
protected:
	STRATUM_API virtual void OnLateUpdate(CommandBuffer& commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera, const std::shared_ptr<DescriptorSet>& perCamera) override;
	
	STRATUM_API void OnGui(CommandBuffer& commandBuffer, Camera& camera, GuiContext& gui);

private:
	std::shared_ptr<stm::Material> mMaterial;
	std::shared_ptr<stm::Mesh> mMesh;
	AABB mAABB;
	stm::Mesh* mSkinnedMesh;

	std::map<std::string, Bone*> mBoneMap;
	AnimationRig mRig;
};

}