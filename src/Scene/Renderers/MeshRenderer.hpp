#pragma once

#include "../../Core/Asset/Mesh.hpp"
#include "Renderer.hpp"

namespace stm {

// Renders a mesh with a material
// The scene will attempt to batch MeshRenderers that share the same mesh and material that have an 'Instances' parameter, and use instancing to render them all at once
class MeshRenderer : public Renderer {
public:
	inline static const uint32_t gInstanceBatchSize = 1024;

	inline MeshRenderer(SceneNode& node, const string& name) : SceneNode::Component(node, name) {};
	
	inline virtual void Mesh(shared_ptr<stm::Mesh> m) { mMesh = m; }
	inline virtual shared_ptr<stm::Mesh> Mesh() const { return mMesh; }
	inline virtual shared_ptr<stm::Material> Material() { return mMaterial; }
	inline virtual void Material(shared_ptr<stm::Material> m) { mMaterial = m; }

	inline virtual bool Visible(Camera& camera) override {
		Vector3f sz = mAABB.sizes();
		return mMesh && Material() && ranges::all_of(camera.LocalFrustum(), [&](const auto& plane) { return signedDistance(plane, mAABB) > 0; });
	}

private:
	shared_ptr<stm::Material> mMaterial;
	shared_ptr<stm::Mesh> mMesh;
	AlignedBox3f mAABB;

protected:
	friend class Scene;

	inline virtual void OnValidateTransform(Matrix4f& globalTransform, TransformTraits& globalTransformTraits) override {
		mAABB = globalTransform;
		if (mMesh && mMesh->AABB())
			mAABB = globalTransform * mMesh->AABB();
	}
	inline virtual void OnDraw(CommandBuffer& commandBuffer, Camera& camera) override {
		mMaterial->Bind(commandBuffer, mMesh.get());
		mMesh->Draw(commandBuffer);
	}
};

}