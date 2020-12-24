#pragma once

#include "../../Core/Asset/Mesh.hpp"
#include "../Bone.hpp"
#include "Renderer.hpp"

namespace stm {

using AnimationRig = vector<Bone*>;

class SkinnedMeshRenderer : public Renderer {
public:
	inline SkinnedMeshRenderer(const string& name, stm::Scene& scene) : Object(name, scene) {};

	STRATUM_API virtual AnimationRig& Rig() { return mRig; };
	STRATUM_API virtual void Rig(const AnimationRig& rig);
	STRATUM_API virtual Bone* GetBone(const string& name) const;

	inline virtual void Mesh(shared_ptr<stm::Mesh> m);
	inline virtual shared_ptr<stm::Mesh> Mesh() const { return mMesh; }

	inline virtual shared_ptr<stm::Material> Material() { return mMaterial; }
	STRATUM_API virtual void Material(shared_ptr<stm::Material> m) { mMaterial = m; }

	inline virtual bool Visible(const string& pass) override { return mMesh && mMaterial && Renderer::Visible(pass); }
	
protected:
	STRATUM_API virtual void OnLateUpdate(CommandBuffer& commandBuffer) override;
	STRATUM_API virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override;
	
	STRATUM_API void OnGui(CommandBuffer& commandBuffer, GuiContext& gui);

private:
	shared_ptr<stm::Material> mMaterial;
	shared_ptr<stm::Mesh> mMesh;
	fAABB mAABB;
	stm::Mesh* mSkinnedMesh;

	map<string, Bone*> mBoneMap;
	AnimationRig mRig;
};

}