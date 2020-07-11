#pragma once

#include <Content/Animation.hpp>
#include <Scene/MeshRenderer.hpp>

class SkinnedMeshRenderer : public Renderer {
public:
	bool mVisible;
	
	ENGINE_EXPORT SkinnedMeshRenderer(const std::string& name);
	ENGINE_EXPORT ~SkinnedMeshRenderer();

	inline virtual float ShapeKey(const std::string& name) const { return mShapeKeys.at(name); };
	inline virtual void ShapeKey(const std::string& name, float val) { mShapeKeys[name] = val; };

	ENGINE_EXPORT virtual AnimationRig& Rig() { return mRig; };
	ENGINE_EXPORT virtual void Rig(const AnimationRig& rig);
	ENGINE_EXPORT virtual Bone* GetBone(const std::string& name) const;

	inline virtual void Mesh(::Mesh* m) { mMesh = m; Dirty(); }
	inline virtual void Mesh(std::shared_ptr<::Mesh> m) { mMesh = m; Dirty(); }
	inline virtual ::Mesh* Mesh() const { return mMesh.index() == 0 ? std::get<::Mesh*>(mMesh) : std::get<std::shared_ptr<::Mesh>>(mMesh).get(); }

	inline virtual ::Material* Material() { return mMaterial.get(); }
	ENGINE_EXPORT virtual void Material(std::shared_ptr<::Material> m) { mMaterial = m; }

	// Renderer functions

	inline virtual PassType PassMask() override { return mMaterial ? mMaterial->PassMask() : Renderer::PassMask(); }
	inline virtual bool Visible() override { return mVisible && Mesh() && mMaterial && EnabledHierarchy(); }
	inline virtual uint32_t RenderQueue() override { return mMaterial ? mMaterial->RenderQueue() : Renderer::RenderQueue(); }
	
	ENGINE_EXPORT virtual void PostUpdate(CommandBuffer* commandBuffer) override;
	ENGINE_EXPORT virtual void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;
	
	ENGINE_EXPORT void DrawGUI(CommandBuffer* commandBuffer, Camera* camera);

private:
	std::shared_ptr<::Material> mMaterial;
	std::variant<::Mesh*, std::shared_ptr<::Mesh>> mMesh;
	AABB mAABB;

	Buffer* mVertexBuffer;

	std::unordered_map<std::string, Bone*> mBoneMap;
	AnimationRig mRig;
	std::unordered_map<std::string, float> mShapeKeys;

	ENGINE_EXPORT bool UpdateTransform() override;
};