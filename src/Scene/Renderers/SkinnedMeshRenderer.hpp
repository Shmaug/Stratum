#pragma once

#include "../../Core/Asset/Mesh.hpp"
#include "Renderer.hpp"

namespace stm {

class Bone : public SceneNode::Component {
public:
	uint32_t mBoneIndex;
	Matrix4f mInverseBind;
	inline Bone(SceneNode& node, const string& name, uint32_t index) : SceneNode::Component(node, name), mBoneIndex(index), mInverseBind(Matrix4f::Identity()) {}
};
using AnimationRig = vector<Bone*>;

class SkinnedMeshRenderer : public Renderer {
public:
	inline SkinnedMeshRenderer(const string& name, stm::Scene& scene) : SceneNode::Component(name, scene) {};

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
	inline virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override {
		mMaterial->Bind(commandBuffer, mSkinnedMesh);
		mSkinnedMesh->Draw(commandBuffer);
	}
	
private:
	shared_ptr<stm::Material> mMaterial;
	shared_ptr<stm::Mesh> mMesh;
	AlignedBox3f mAABB;
	stm::Mesh* mSkinnedMesh;

	unordered_map<string, Bone*> mBoneMap;
	AnimationRig mRig;
};

}